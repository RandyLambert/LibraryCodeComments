/*
 * @Date: 2020-8-17 18:27:46
 * @LastEditors: OBKoro1
 * @LastEditTime: 2020-12-06 14:29:23
 * @FilePath: /LibraryCodeComments/epoll/eventpoll.c
 * @Auther: SShouxun
 * @GitHub: https://github.com/RandyLambert
 */

// epoll 维护了一棵红黑树来跟踪所有待检测的文件描述字,黑红树的使用减少了内核和用户
// 空间大量的数据拷贝和内存分配,提高了性能.
// 同时，epoll 维护了一个链表来记录就绪事件,内核在每个文件有事件发生时将自己登记到
// 这个就绪事件列表中,通过内核自身的文件 file-eventpoll 之间的回调和唤醒机制,减少了
// 对内核描述字的遍历,大大加速了事件通知和检测的效率

// epoll 中主要使用了三个数据结构,分别是 eventpoll , epitem , eppoll_entry
// 对于 eventpoll 这个数据结构,这个数据结构是我们在调用 epoll_create 之后在内核中创建了一个句柄,表示一个 epoll 实例,
// 后续调用epol_ctl和epoll_wait 这些函数都是对这个 eventpoll 数据进行操作,这部分数据会被保存在 epoll_create 创建
// 的匿名文件 file 的 private_data 字段中.
/*
 * This structure is stored inside the "private_data" member of the file
 * structure and represents the main data structure for the eventpoll
 * interface.
 */
struct eventpoll {
	/*
	 * This mutex is used to ensure that files are not removed
	 * while epoll is using them. This is held during the event
	 * collection loop, the file cleanup path, the epoll file exit
	 * code and the ctl operations.
	 */
	struct mutex mtx;

	/* Wait queue used by sys_epoll_wait() */
    // 这个队列存的是执行 epoll_wait 从而等待的进程队列
	wait_queue_head_t wq;

	/* Wait queue used by file->poll() */
    // 这个队列里存的是改 eventloop 作为 poll 对象的一个实例,加入到等待
    // 的队列,这是因为 eventpoll 本事也是 file, 所以也会有 poll 操作
	wait_queue_head_t poll_wait;

	/* List of ready file descriptors */
    // 这里放的是事件就绪的 fd 列表,链表的每个元素是上面介绍的 epitem
	struct list_head rdllist;

	/* Lock which protects rdllist and ovflist */
	rwlock_t lock;

	/* RB tree root used to store monitored fd structs */
    // 这里是用来快速查找 fd 的红黑树
	struct rb_root_cached rbr;

	/*
	 * This is a single linked list that chains all the "struct epitem" that
	 * happened while transferring ready events to userspace w/out
	 * holding ->lock.
	 */
	struct epitem *ovflist;

	/* wakeup_source used when ep_scan_ready_list is running */
	// 当 ep_scan_ready_list 运行是使用的 wakeup_source
	struct wakeup_source *ws;

	/* The user that created the eventpoll descriptor */
	// 创建 eventpoll 描述符的用户
	struct user_struct *user;
    // 这是 eventloop 对应的匿名文件,Linux下一切皆文件
	struct file *file;

	/* used to optimize loop detection check */
	u64 gen;

#ifdef CONFIG_NET_RX_BUSY_POLL
	/* used to track busy poll napi_id */
	unsigned int napi_id;
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/* tracks wakeup nests for lockdep validation */
	u8 nests;
#endif
};

// epitem 这个数据结构,是我们每次调用 epoll_ctl 增加一个 fd 时,内核就会为我们创建出一个 epitem 实例,并且把这个实例
// 作为红黑树的一个子节点,增加到 eventpoll 结构体中的红黑树中,对应的字段就是 rbr , 因此,查找每个 fd 上是否有事件发生
// 都是通过红黑树上的 epitem 来操作的
struct epitem {
	union {
		/* RB tree node links this structure to the eventpoll RB tree */
		// RB 树节点将此结构链接到 eventpoll RB树
		struct rb_node rbn;
		/* Used to free the struct epitem */
		// 用于释放结构体 epitem
		struct rcu_head rcu;
	};

	/* List header used to link this structure to the eventpoll ready list */
	// 将这个 epitem 链接到 eventpoll 里面的 rdlist 的 list 指针
    struct list_head rdllink;

	/*
	 * Works together "struct eventpoll"->ovflist in keeping the
	 * single linked chain of items.
	 */
	// 配合 ovflist 一起使用来保持单向链的条目
	struct epitem *next;

	/* The file descriptor information this item refers to */
	// epoll 监听的 fd,下面有详细介绍
    struct epoll_filefd ffd;

	/* Number of active wait queue attached to poll operations */
	// 一个文件可以被多个 epoll 实例监听,这里记录当前文件被监听的次数
    int nwait;

	/* List containing poll wait queues */
	struct list_head pwqlist;

	/* The "container" of this item */
    // 当前 epollitem 所属的 eventpoll
	struct eventpoll *ep;

	/* List header used to link this item to the "struct file" items list */
	// 链接到 file 条目的列表的列表头
	struct list_head fllink;

	/* wakeup_source used when EPOLLWAKEUP is set */
	// 设置 EPOLLWAKEUP 时使用的 wakeup_source
	struct wakeup_source __rcu *ws;

	/* The structure that describe the interested events and the source fd */
	// 监控的事件与文件描述符
	struct epoll_event event;
};
// 监控的事件与文件描述符
struct epoll_event {
    __u32 events;
    __u64 data;
} EPOLL_PACKED;

// epoll 上挂的文件描述符.
struct epoll_filefd {
	struct file *file;
	int fd;
} __packed;
// 用红黑树存储,需要定义排序规则,
// 规则是按照文件的地址大小来排序,如果两个相同,就按照文件的文件描述字来排序
/* Compare RB tree keys */
static inline int ep_cmp_ffd(struct epoll_filefd *p1,
			     struct epoll_filefd *p2)
{
	return (p1->file > p2->file ? +1:
	        (p1->file < p2->file ? -1 : p1->fd - p2->fd));
}

// 每当有一个 fd 关联到了一个 epoll 实例,就会有一个 eppoll_entry 产生
/* Wait structure used by the poll hooks */
struct eppoll_entry {
	/* List header used to link this structure to the "struct epitem" */
	// 指向 epitem 的列表头
	struct list_head llink;

	/* The "base" pointer is set to the container "struct epitem" */
	// 指向 epitem 的指针
	struct epitem *base;

	/*
	 * Wait queue item that will be linked to the target file wait
	 * queue head.
	 */
	// 指向 target file 等待队列
	wait_queue_entry_t wait;

	/* The wait queue head that linked the "wait" wait queue item */
	// 执行 wait 的等待队列
	wait_queue_head_t *whead;
};

// 当我们在使用 epoll 的时候,首先会调用 epoll_create 来创建一个 epoll 实例
/*
 * Open an eventpoll file descriptor.
 */
static int do_epoll_create(int flags)
{
	int error, fd;
	struct eventpoll *ep = NULL;
	struct file *file;

	/* Check the EPOLL_* constant for consistency.  */
	// 对 flags 参数做验证
    BUILD_BUG_ON(EPOLL_CLOEXEC != O_CLOEXEC);

	if (flags & ~EPOLL_CLOEXEC)
		return -EINVAL;
	/*
	 * Create the internal data structure ("struct eventpoll").
	 */
    // 内核申请分配 eventpoll 需要的内存空间
	error = ep_alloc(&ep);
	if (error < 0)
		return error;
	/*
	 * Creates all the items needed to setup an eventpoll file. That is,
	 * a file structure and a free file descriptor.
	 */
    // 分配匿名文件和文件描述符
    // fd 是文件描述符
	fd = get_unused_fd_flags(O_RDWR | (flags & O_CLOEXEC));
	if (fd < 0) {
		error = fd;
		goto out_free_ep;
	}

    // file 是文件,fd 与 file 一一对应
    // 在调用 anon_inode_get_file 的时候,epoll_create 将eventpoll 作为匿名文件
    // file 的 private_data 保存起来,在之后通过 epoll 实例的文件描述符查找的时候,就可以快速定位 eventpoll 对象
	file = anon_inode_getfile("[eventpoll]", &eventpoll_fops, ep,
				 O_RDWR | (flags & O_CLOEXEC));
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		goto out_free_fd;
	}
	ep->file = file;
    // eventpoll 的实例会保存一份匿名文件的引用,通过调用 fd_install 函数将匿名文件和文件描述符完成了绑定
	fd_install(fd, file);
    // 最后文件描述符作为 epoll 的文件句柄,被返回给 epoll_create 的调用者
	return fd;

out_free_fd:
	put_unused_fd(fd);
out_free_ep:
	ep_free(ep);
	return error;
}

SYSCALL_DEFINE1(epoll_create1, int, flags)
{
	return do_epoll_create(flags);
}

SYSCALL_DEFINE1(epoll_create, int, size)
{
	if (size <= 0)
		return -EINVAL;

	return do_epoll_create(0);
}

// 一个套接字是如何添加到 epoll 实例之中的,是通过 epoll_ctl 函数实现的
int do_epoll_ctl(int epfd, int op, int fd, struct epoll_event *epds,
		 bool nonblock)
{
	int error;
	int full_check = 0;
	struct fd f, tf;
	struct eventpoll *ep;
	struct epitem *epi;
	struct eventpoll *tep = NULL;

	error = -EBADF;
    // UNIX 下一切都是文件, epoll 的实例是一个匿名文件, epoll_ctl 函数通过 epoll 实例句柄来获得对应的匿名文件
    // 获取 epoll 实例对应的匿名文件
	f = fdget(epfd); // 在上面的函数中,文件描述符和匿名文件做了一一对应
	if (!f.file)
		goto error_return;

	/* Get the "struct file *" for the target file */
    // 获得真正的文件,如监听套接字,读写套接字
    // tf 表示的是 target file,即待处理的目标文件
	tf = fdget(fd);
	if (!tf.file)
		goto error_fput;
    // 进行一些列的数据验证,以确保用户传入的参数是合法的,比如 epfd 是一个 epoll 实例句柄,二不是一个普通的文件描述符
	/* The target file descriptor must support poll */
    // 如果不支持 poll ,那么该文件描述符是无效的
	error = -EPERM;
	if (!file_can_poll(tf.file))
		goto error_tgt_fput;

	/* Check if EPOLLWAKEUP is allowed */
	if (ep_op_has_event(op))
		ep_take_care_of_epollwakeup(epds);

	/*
	 * We have to check that the file structure underneath the file descriptor
	 * the user passed to us _is_ an eventpoll file. And also we do not permit
	 * adding an epoll file descriptor inside itself.
	 */
	error = -EINVAL;
	if (f.file == tf.file || !is_file_epoll(f.file))
		goto error_tgt_fput;

	/*
	 * epoll adds to the wakeup queue at EPOLL_CTL_ADD time only,
	 * so EPOLLEXCLUSIVE is not allowed for a EPOLL_CTL_MOD operation.
	 * Also, we do not currently supported nested exclusive wakeups.
	 */
	if (ep_op_has_event(op) && (epds->events & EPOLLEXCLUSIVE)) {
		if (op == EPOLL_CTL_MOD)
			goto error_tgt_fput;
		if (op == EPOLL_CTL_ADD && (is_file_epoll(tf.file) ||
				(epds->events & ~EPOLLEXCLUSIVE_OK_BITS)))
			goto error_tgt_fput;
	}

	/*
	 * At this point it is safe to assume that the "private_data" contains
	 * our own data structure.
	 */
    // 如果获得了一个真正的 epoll 实例句柄,就可以通过 private_data 获取之前创建的 eventpoll 实例了
	ep = f.file->private_data; // 在上面的 create 函数中把 eventpoll 绑定到了该匿名文件的 private_data

	/*
	 * When we insert an epoll file descriptor, inside another epoll file
	 * descriptor, there is the change of creating closed loops, which are
	 * better be handled here, than in more critical paths. While we are
	 * checking for loops we also determine the list of files reachable
	 * and hang them on the tfile_check_list, so we can check that we
	 * haven't created too many possible wakeup paths.
	 *
	 * We do not need to take the global 'epumutex' on EPOLL_CTL_ADD when
	 * the epoll file descriptor is attaching directly to a wakeup source,
	 * unless the epoll file descriptor is nested. The purpose of taking the
	 * 'epmutex' on add is to prevent complex toplogies such as loops and
	 * deep wakeup paths from forming in parallel through multiple
	 * EPOLL_CTL_ADD operations.
	 */
	error = epoll_mutex_lock(&ep->mtx, 0, nonblock);
	if (error)
		goto error_tgt_fput;
	if (op == EPOLL_CTL_ADD) {
		if (!list_empty(&f.file->f_ep_links) ||
				ep->gen == loop_check_gen ||
						is_file_epoll(tf.file)) {
			mutex_unlock(&ep->mtx);
			error = epoll_mutex_lock(&epmutex, 0, nonblock);
			if (error)
				goto error_tgt_fput;
			loop_check_gen++;
			full_check = 1;
			if (is_file_epoll(tf.file)) {
				error = -ELOOP;
				if (ep_loop_check(ep, tf.file) != 0)
					goto error_tgt_fput;
			} else {
				get_file(tf.file);
				list_add(&tf.file->f_tfile_llink,
							&tfile_check_list);
			}
			error = epoll_mutex_lock(&ep->mtx, 0, nonblock);
			if (error)
				goto error_tgt_fput;
			if (is_file_epoll(tf.file)) {
				tep = tf.file->private_data;
				error = epoll_mutex_lock(&tep->mtx, 1, nonblock);
				if (error) {
					mutex_unlock(&ep->mtx);
					goto error_tgt_fput;
				}
			}
		}
	}

	/*
	 * Try to lookup the file inside our RB tree, Since we grabbed "mtx"
	 * above, we can be sure to be able to use the item looked up by
	 * ep_find() till we release the mutex.
	 */
    // 红黑树查找,epoll_ctl 通过通过目标文件和对应描述字，在红黑树中查找是否存在该套接字，这也是
    // epoll 为什么高效的地方。红黑树（RB-tree）是一种常见的数据结构，这里 eventpoll 通
    //过红黑树跟踪了当前监听的所有文件描述字，而这棵树的根就保存在 eventpoll 数据结构中。
    // 对于每个监听的文件描述符,都有一个对应的 epitem 与之对应,epitem 作为红黑树中的节点就保存在红黑树
	// 红黑树是一颗二叉树,作为二叉树上的节点,epitem 必须提供比较能力,一遍可以按照大小顺序构建出一颗有序的
    // 二叉树,其排序能力是依靠 epoll_filefd 结构体来完成的,epoll_filefd 可以简单的理解为需要监听的文件
    // 描述符,对应而常熟上的节点
    epi = ep_find(ep, tf.file, fd);

    //在进行完红黑树查找之后，如果发现是一个 ADD 操作，并且在树中没有找到对应的二叉树
    //节点，就会调用 ep_insert 进行二叉树节点的增加
    // ep_insert 函数解析在下面
	error = -EINVAL;
	switch (op) {
	case EPOLL_CTL_ADD:
		if (!epi) {
			epds->events |= EPOLLERR | EPOLLHUP;
			error = ep_insert(ep, epds, tf.file, fd, full_check);
		} else
			error = -EEXIST;
		break;
	case EPOLL_CTL_DEL:
		if (epi)
			error = ep_remove(ep, epi);
		else
			error = -ENOENT;
		break;
	case EPOLL_CTL_MOD:
		if (epi) {
			if (!(epi->event.events & EPOLLEXCLUSIVE)) {
				epds->events |= EPOLLERR | EPOLLHUP;
				error = ep_modify(ep, epi, epds);
			}
		} else
			error = -ENOENT;
		break;
	}
	if (tep != NULL)
		mutex_unlock(&tep->mtx);
	mutex_unlock(&ep->mtx);

error_tgt_fput:
	if (full_check) {
		clear_tfile_check_list();
		loop_check_gen++;
		mutex_unlock(&epmutex);
	}

	fdput(tf);
error_fput:
	fdput(f);
error_return:

	return error;
}

// 文件描述符二叉树插入函数
/*
 * Must be called with "mtx" held.
 */
static int ep_insert(struct eventpoll *ep, const struct epoll_event *event,
		     struct file *tfile, int fd, int full_check)
{
	int error, pwake = 0;
	__poll_t revents;
	long user_watches;
	struct epitem *epi;
	struct ep_pqueue epq;
    // ep_insert 先判断当前监控的文件值是否超过了 /proc/sys/fs/epoll/max_user_watches
    // 的预设最大值，如果超过了则直接返回错误。
	lockdep_assert_irqs_enabled();

	user_watches = atomic_long_read(&ep->user->epoll_watches);
	if (unlikely(user_watches >= max_user_watches))
		return -ENOSPC;
    // 然后进行分配资源和初始化的动作
	if (!(epi = kmem_cache_alloc(epi_cache, GFP_KERNEL)))
		return -ENOMEM;

	/* Item initialization follow here ... */
	INIT_LIST_HEAD(&epi->rdllink);
	INIT_LIST_HEAD(&epi->fllink);
	INIT_LIST_HEAD(&epi->pwqlist);
	epi->ep = ep;
	ep_set_ffd(&epi->ffd, tfile, fd);
	epi->event = *event;
	epi->nwait = 0;
	epi->next = EP_UNACTIVE_PTR;
	if (epi->event.events & EPOLLWAKEUP) {
		error = ep_create_wakeup_source(epi);
		if (error)
			goto error_create_wakeup_source;
	} else {
		RCU_INIT_POINTER(epi->ws, NULL);
	}

	/* Add the current item to the list of active epoll hook for this file */
	spin_lock(&tfile->f_lock);
	list_add_tail_rcu(&epi->fllink, &tfile->f_ep_links);
	spin_unlock(&tfile->f_lock);

	/*
	 * Add the current item to the RB tree. All RB tree operations are
	 * protected by "mtx", and ep_insert() is called with "mtx" held.
	 */
	ep_rbtree_insert(ep, epi);

	/* now check if we've created too many backpaths */
	error = -EINVAL;
	if (full_check && reverse_path_check())
		goto error_remove_epi;

	/* Initialize the poll table using the queue callback */
	// 设立回调函数,ep_insert 会为加入的每个文件描述符设置回调函数
    // 这个回调函数是通过函数 ep_ptable_queue_proc 来进行设置的,如果对应的文件描述符
    // 有事件发生,就会回调这个函数,这个函数就是 ep_poll_callback
    epq.epi = epi;
	init_poll_funcptr(&epq.pt, ep_ptable_queue_proc);

	/*
	 * Attach the item to the poll hooks and get current event bits.
	 * We can safely use the file* here because its usage count has
	 * been increased by the caller of this function. Note that after
	 * this operation completes, the poll callback can start hitting
	 * the new item.
	 */
	revents = ep_item_poll(epi, &epq.pt, 1);

	/*
	 * We have to check if something went wrong during the poll wait queue
	 * install process. Namely an allocation for a wait queue failed due
	 * high memory pressure.
	 */
	error = -ENOMEM;
	if (epi->nwait < 0)
		goto error_unregister;

	/* We have to drop the new item inside our item list to keep track of it */
	write_lock_irq(&ep->lock);

	/* record NAPI ID of new item if present */
	ep_set_busy_poll_napi_id(epi);

	/* If the file is already "ready" we drop it inside the ready list */
	if (revents && !ep_is_linked(epi)) {
		list_add_tail(&epi->rdllink, &ep->rdllist);
		ep_pm_stay_awake(epi);

		/* Notify waiting tasks that events are available */
		if (waitqueue_active(&ep->wq))
			wake_up(&ep->wq);
		if (waitqueue_active(&ep->poll_wait))
			pwake++;
	}

	write_unlock_irq(&ep->lock);

	atomic_long_inc(&ep->user->epoll_watches);

	/* We have to call this outside the lock */
	if (pwake)
		ep_poll_safewake(ep, NULL);

	return 0;

error_unregister:
	ep_unregister_pollwait(ep, epi);
error_remove_epi:
	spin_lock(&tfile->f_lock);
	list_del_rcu(&epi->fllink);
	spin_unlock(&tfile->f_lock);

	rb_erase_cached(&epi->rbn, &ep->rbr);

	/*
	 * We need to do this because an event could have been arrived on some
	 * allocated wait queue. Note that we don't care about the ep->ovflist
	 * list, since that is used/cleaned only inside a section bound by "mtx".
	 * And ep_insert() is called with "mtx" held.
	 */
	write_lock_irq(&ep->lock);
	if (ep_is_linked(epi))
		list_del_init(&epi->rdllink);
	write_unlock_irq(&ep->lock);

	wakeup_source_unregister(ep_wakeup_source(epi));

error_create_wakeup_source:
	kmem_cache_free(epi_cache, epi);

	return error;
}


/*
 * This is the callback that is used to add our wait queue to the
 * target file wakeup lists.
 */
static void ep_ptable_queue_proc(struct file *file, wait_queue_head_t *whead,
				 poll_table *pt)
{
	struct epitem *epi = ep_item_from_epqueue(pt);
	struct eppoll_entry *pwq;

	if (epi->nwait >= 0 && (pwq = kmem_cache_alloc(pwq_cache, GFP_KERNEL))) {
		init_waitqueue_func_entry(&pwq->wait, ep_poll_callback);
		pwq->whead = whead;
		pwq->base = epi;
		if (epi->event.events & EPOLLEXCLUSIVE)
			add_wait_queue_exclusive(whead, &pwq->wait);
		else
			add_wait_queue(whead, &pwq->wait);
		list_add_tail(&pwq->llink, &epi->pwqlist);
		epi->nwait++;
	} else {
		/* We have to signal that an error occurred */
		epi->nwait = -1;
	}
}

// ep_poll_call_back 函数的作用十分重要,它将内核事件真正的和 epoll 对象联系起来了
static int ep_poll_callback(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
    int pwake = 0;
	// 首先这个文件的 wait_queue_entry_t 对象找到对应的 epitem 对象，因为
    // eppoll_entry 对象里保存了 wait_quue_entry_t,根据 wait_quue_entry_t 这个对象的地
    // 址就可以简单计算出 eppoll_entry 对象的地址,从而可以获得 epitem 对象的地址。这部
    // 分工作在 ep_item_from_wait 函数中完成.一旦获得 epitem 对象,就可以寻迹找到
    // eventpoll 实例
    unsigned long flags;
    struct epitem *epi = ep_item_from_wait(wait);
    struct eventpoll *ep = epi->ep;
	// 加锁
    spin_lock_irqsave(&ep->lock, flags);

	// 下面对发生的事件进行过滤,为了性能考虑,ep_insert 向对应监控文
	// 件注册的是所有的事件,而实际用户侧订阅的事件未必和内核事件对应.比如,用户向内核
	// 订阅了一个套接字的可读事件,在某个时刻套接字的可写事件发生时,并不需要向用户空间
	// 传递这个事件.
	if (key && !((unsigned long) key & epi->event.events))
		goto out_unlock;

	// 这里判断是否需要把事件传递到用户空间
    // 如果正在将事件传递给用户空间,我们就不能保持锁定
    // (因为我们正在访问用户内存,并且因为linux f_op-> poll()语义).
    // 在那段时间内发生的所有事件都链接在ep-> ovflist中并在稍后重新排队.
	if (unlikely(ep->ovflist != EP_UNACTIVE_PTR)) {
        if (epi->next == EP_UNACTIVE_PTR) {
            epi->next = ep->ovflist;
            ep->ovflist = epi;
            if (epi->ws) {
                __pm_stay_awake(ep->ws);
            }
        }
        goto out_unlock;
    }

    //如果此文件已在就绪列表中，很快就会退出
    if (!ep_is_linked(&epi->rdllink)) {
        //将epi就绪事件 插入到ep就绪队列
        list_add_tail(&epi->rdllink, &ep->rdllist);
        ep_pm_stay_awake_rcu(epi);
    }

	// 当我们调用 epoll_wait的时候,调用进程被挂起,在内核中调用进程陷入休眠
	// 如果epoll 实例上面有时间发生,这个休眠进程应该被唤醒,一遍及时处理时间,
	// 下面的代码就是起的这个作用,wake_up 函数唤醒当前 eventpoll 上的等待进程
	if (waitqueue_active(&ep->wq)) {
		if ((epi->event.events & EPOLLEXCLUSIVE) &&
					!(pollflags & POLLFREE)) {
			switch (pollflags & EPOLLINOUT_BITS) {
			case EPOLLIN:
				if (epi->event.events & EPOLLIN)
					ewake = 1;
				break;
			case EPOLLOUT:
				if (epi->event.events & EPOLLOUT)
					ewake = 1;
				break;
			case 0:
				ewake = 1;
				break;
			}
		}
		// 唤醒等待进程
		wake_up_locked(&ep->wq);
	}
	if (waitqueue_active(&ep->poll_wait))
		pwake++;

out_unlock:
    spin_unlock_irqrestore(&ep->lock, flags);
    if (pwake)
        ep_poll_safewake(&ep->poll_wait);

    if ((unsigned long)key & POLLFREE) {
        list_del_init(&wait->task_list); //删除相应的wait
        smp_store_release(&ep_pwq_from_wait(wait)->whead, NULL);
    }
    return 1;
}

// 判断等待队列是否为空
static inline int waitqueue_active(wait_queue_head_t *q)
{
	return !list_empty(&q->task_list);
}


/*
 * Implement the event wait interface for the eventpoll file. It is the kernel
 * part of the user space epoll_wait(2).
 */
static int do_epoll_wait(int epfd, struct epoll_event __user *events,
			 int maxevents, int timeout)
{
	int error;
	struct fd f;
	struct eventpoll *ep;

	/* The maximum number of event must be greater than zero */
	// epoll_wait 函数首先进行一系列检查,例如传入的 maxevents 应该大于 0 .
	if (maxevents <= 0 || maxevents > EP_MAX_EVENTS)
		return -EINVAL;

	/* Verify that the area passed by the user is writeable */
	if (!access_ok(events, maxevents * sizeof(struct epoll_event)))
		return -EFAULT;

	/* Get the "struct file *" for the eventpoll file */
	// 和 epoll_ctl 一样,通过 epoll 实例找到对应的匿名文件和描述符,并且进行检查和验证
	f = fdget(epfd);
	if (!f.file)
		return -EBADF;

	/*
	 * We have to check that the file structure underneath the fd
	 * the user passed to us _is_ an eventpoll file.
	 */
	error = -EINVAL;
	if (!is_file_epoll(f.file))
		goto error_fput;

	/*
	 * At this point it is safe to assume that the "private_data" contains
	 * our own data structure.
	 */
	// 同上通过读取 epoll 实例对应匿名文件的 private_data 得到 eventpoll 实例。
	ep = f.file->private_data;

	/* Time to fish for events ... */
	// 调用 ep_poll 来完成对应的事件收集并传递到用户空间
	error = ep_poll(ep, events, maxevents, timeout);

error_fput:
	fdput(f);
	return error;
}

/**
 * ep_poll - Retrieves ready events, and delivers them to the caller supplied
 *           event buffer.
 *
 * @ep: Pointer to the eventpoll context.
 * @events: Pointer to the userspace buffer where the ready events should be
 *          stored.
 * @maxevents: Size (in terms of number of events) of the caller event buffer.
 * @timeout: Maximum timeout for the ready events fetch operation, in
 *           milliseconds. If the @timeout is zero, the function will not block,
 *           while if the @timeout is less than zero, the function will block
 *           until at least one event has been retrieved (or an error
 *           occurred).
 *
 * Returns: Returns the number of ready events which have been fetched, or an
 *          error code, in case of error.
 */
// 参数对应的参数 timeout 值可以大于0,等于0,或者小于0,这里的ep_poll函数分别对应 timeout
// 不同值的场景进行了处理
static int ep_poll(struct eventpoll *ep, struct epoll_event __user *events,
           int maxevents, long timeout)
{
    int res = 0, eavail, timed_out = 0;
    unsigned long flags;
    long slack = 0;
    wait_queue_t wait;
    ktime_t expires, *to = NULL;

    if (timeout > 0) { // 如果大于 0 则产生了一个超时时间
        struct timespec end_time = ep_set_mstimeout(timeout);
        slack = select_estimate_accuracy(&end_time);
        to = &expires;
        *to = timespec_to_ktime(end_time);
    } else if (timeout == 0) { // 如果等于 0 则立即检查是否有事情发生
        // timeout等于0为非阻塞操作，此处避免不必要的等待队列循环
        timed_out = 1;
        spin_lock_irqsave(&ep->lock, flags);
        goto check_events;
    }

fetch_events:
    spin_lock_irqsave(&ep->lock, flags);

    if (!ep_events_available(ep)) {  // 检查当前是否有事件发生
        // 没有事件就绪则进入睡眠状态，当事件就绪后可通过ep_poll_callback()来唤醒
        // 将当前进程放入wait等待队列 
        init_waitqueue_entry(&wait, current);
        // 将当前进程加入eventpoll等待队列，等待文件就绪、超时或中断信号
        __add_wait_queue_exclusive(&ep->wq, &wait);

		// 下面是一个无限循环,这个循环中通过调用 schedule_hrtimeout_range,将当前
		// 进程陷入休眠,CPU 时间被调度器调度给其他进程使用,当然,当前进程可能会被唤醒
		// 唤醒的条件包括四种
		// 1. 当前进程超时；
		// 2. 当前进程收到一个 signal 信号；
		// 3. 某个描述字上有事件发生；
		// 4. 当前进程被 CPU 重新调度，进入 for 循环重新判断，如果没有满足前三个条件，就又
		// 重新进入休眠
        for (;;) {
			// 设置当前进程状态
            set_current_state(TASK_INTERRUPTIBLE);
            if (ep_events_available(ep) || timed_out) //就绪队列不为空 或者超时，则跳出循环
                break;
            if (signal_pending(current)) { //有待处理信号，则跳出循环
                res = -EINTR;
                break;
            }

            spin_unlock_irqrestore(&ep->lock, flags);
        	// 通过调用 schedule函数,主动使当前进程进入休眠,cpu 时间被其他进程使用
            if (!freezable_schedule_hrtimeout_range(to, slack,
                                HRTIMER_MODE_ABS))
                timed_out = 1;

            spin_lock_irqsave(&ep->lock, flags);
        }
        __remove_wait_queue(&ep->wq, &wait); //从队列中移除wait
        set_current_state(TASK_RUNNING);
    }
check_events:
    eavail = ep_events_available(ep);
    spin_unlock_irqrestore(&ep->lock, flags);

    // 尝试传输就绪事件到用户空间，如果没有获取就绪事件，但还剩下超时，则会再次retry
    // ep_send_events 将事件拷贝到用户空间
	if (!res && eavail &&
        !(res = ep_send_events(ep, events, maxevents)) && !timed_out)
        goto fetch_events;

    return res;
}

// 这个函数会把这个函数会将 ep_send_events_proc 作为回调函数并调用
// ep_scan_ready_list 函数,ep_scan_ready_list 函数调用 ep_send_events_proc 对每个已
// 经就绪的事件循环处理
static int ep_send_events(struct eventpoll *ep,
			  struct epoll_event __user *events, int maxevents)
{
	struct ep_send_events_data esed;

	esed.maxevents = maxevents;
	esed.events = events;

	ep_scan_ready_list(ep, ep_send_events_proc, &esed, 0, false);
	return esed.res;
}

// ep_send_events_proc 循环处理就绪事件时,会再次调用每个文件描述符的 poll 方法,以便确实有事件发生
// 这是为了确定注册事件在这个时刻还是有效的
static __poll_t ep_send_events_proc(struct eventpoll *ep, struct list_head *head,
			       void *priv)
{
	struct ep_send_events_data *esed = priv;
	__poll_t revents;
	struct epitem *epi, *tmp;
	struct epoll_event __user *uevent = esed->events;
	struct wakeup_source *ws;
	poll_table pt;

	init_poll_funcptr(&pt, NULL);
	esed->res = 0;

	/*
	 * We can loop without lock because we are passed a task private list.
	 * Items cannot vanish during the loop because ep_scan_ready_list() is
	 * holding "mtx" during this call.
	 */
	lockdep_assert_held(&ep->mtx);

	list_for_each_entry_safe(epi, tmp, head, rdllink) {
		if (esed->res >= esed->maxevents)
			break;

		/*
		 * Activate ep->ws before deactivating epi->ws to prevent
		 * triggering auto-suspend here (in case we reactive epi->ws
		 * below).
		 *
		 * This could be rearranged to delay the deactivation of epi->ws
		 * instead, but then epi->ws would temporarily be out of sync
		 * with ep_is_linked().
		 */
		ws = ep_wakeup_source(epi);
		if (ws) {
			if (ws->active)
				__pm_stay_awake(ep->ws);
			__pm_relax(ws);
		}

		list_del_init(&epi->rdllink);

		/*
		 * If the event mask intersect the caller-requested one,
		 * deliver the event to userspace. Again, ep_scan_ready_list()
		 * is holding ep->mtx, so no operations coming from userspace
		 * can change the item.
		 */
		// 这里对其中一个 fd 在次进行 poll 操作,以确认事件
		// 虽然已经考虑的很周全了,但是还有改立是当调用文件的
		// poll 函数之后,用户空间获得的事件通知已经不在有效
		// 这可能是用户空间已经处理了,或者是别的情况,如果套接
		// 字不是非阻塞的,整个进程将会被阻塞,这就是为什么 epoll 
		// 最好需要配合非阻塞套接字
		revents = ep_item_poll(epi, &pt, 1);
		if (!revents)
			continue;

		// 事件结构体拷贝到用户空间需要的数据结构中,这里通过 __put_user 方法
		if (__put_user(revents, &uevent->events) ||
		    __put_user(epi->event.data, &uevent->data)) {
			list_add(&epi->rdllink, head);
			ep_pm_stay_awake(epi);
			if (!esed->res)
				esed->res = -EFAULT;
			return 0;
		}
		esed->res++;
		uevent++;
		if (epi->event.events & EPOLLONESHOT)
			epi->event.events &= EP_PRIVATE_BITS;
		// 这里是针对 epoll LT 的情况,将当前的epoll_item 对象重新
		// 加入 eventpoll 的就绪列表中,在下一次 epoll_wait 调用的时候
		// 这些 epoll_item 对象就会被重新处理
		else if (!(epi->event.events & EPOLLET)) {
			/*
			 * If this file has been added with Level
			 * Trigger mode, we need to insert back inside
			 * the ready list, so that the next call to
			 * epoll_wait() will check again the events
			 * availability. At this point, no one can insert
			 * into ep->rdllist besides us. The epoll_ctl()
			 * callers are locked out by
			 * ep_scan_ready_list() holding "mtx" and the
			 * poll callback will queue them in ep->ovflist.
			 */
			list_add_tail(&epi->rdllink, &ep->rdllist);
			ep_pm_stay_awake(epi);
		}
	}

	return 0;
}

