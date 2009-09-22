/*
 * Copyright (c) 1995 Danny Gasparovski
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

/*
 * mbuf's in SLiRP are much simpler than the real mbufs in
 * FreeBSD.  They are fixed size, determined by the MTU,
 * so that one whole packet can fit.  Mbuf's cannot be
 * chained together.  If there's more data than the mbuf
 * could hold, an external malloced buffer is pointed to
 * by m_ext (and the data pointers) and M_EXT is set in
 * the flags
 */

#include <slirp.h>


void
m_init(PNATState pData)
{
    m_freelist.m_next = m_freelist.m_prev = &m_freelist;
    m_usedlist.m_next = m_usedlist.m_prev = &m_usedlist;
    mbuf_alloced = 0;
    msize_init(pData);
}

void
msize_init(PNATState pData)
{
    /*
     * Find a nice value for msize
     */
    msize = (if_mtu>if_mru ? if_mtu : if_mru) 
          + sizeof(struct m_hdr );
}

/*
 * Get an mbuf from the free list, if there are none
 * malloc one
 *
 * Because fragmentation can occur if we alloc new mbufs and
 * free old mbufs, we mark all mbufs above mbuf_thresh as M_DOFREE,
 * which tells m_free to actually free() it
 */
struct mbuf *
m_get(PNATState pData)
{
    register struct mbuf *m;
    int flags = 0;

    DEBUG_CALL("m_get");

    if (m_freelist.m_next == &m_freelist)
    {
        m = (struct mbuf *)RTMemAlloc(msize);
        if (m == NULL)
            goto end_error;
        mbuf_alloced++;
        if (mbuf_alloced > mbuf_thresh)
            flags = M_DOFREE;
        if (mbuf_alloced > mbuf_max)
            mbuf_max = mbuf_alloced;
    }
    else
    {
        m = m_freelist.m_next;
        remque(pData, m);
    }

    /* Insert it in the used list */
    insque(pData, m, &m_usedlist);
    m->m_flags = (flags | M_USEDLIST);

    /* Initialise it */
    m->m_size = msize - sizeof(struct m_hdr);
    m->m_data = m->m_dat;
    m->m_len = 0;
    m->m_nextpkt = 0;
    m->m_prevpkt = 0;
    m->m_la = NULL;
    memset(m->m_data, 0, if_maxlinkhdr); /*initialization of ether area */

end_error:
    DEBUG_ARG("m = %lx", (long )m);
    return m;
}

void
m_free(PNATState pData, struct mbuf *m)
{

    DEBUG_CALL("m_free");
    DEBUG_ARG("m = %lx", (long )m);

    if(m)
    {
        /* Remove from m_usedlist */
        if (m->m_flags & M_USEDLIST)
            remque(pData, m);

        /* If it's M_EXT, free() it */
        if (m->m_flags & M_EXT)
            RTMemFree(m->m_ext);

        /*
         * Either free() it or put it on the free list
         */
        if (m->m_flags & M_DOFREE)
        {
            RTMemFree(m);
            mbuf_alloced--;
        }
        else if ((m->m_flags & M_FREELIST) == 0)
        {
            insque(pData, m,&m_freelist);
            m->m_flags = M_FREELIST; /* Clobber other flags */
        }
    } /* if(m) */
}

/*
 * Copy data from one mbuf to the end of
 * the other.. if result is too big for one mbuf, malloc()
 * an M_EXT data segment
 */
void
m_cat(PNATState pData, register struct mbuf *m, register struct mbuf *n)
{
    /*
     * If there's no room, realloc
     */
    if (M_FREEROOM(m) < n->m_len)
        m_inc(m,m->m_size+MINCSIZE);

    memcpy(m->m_data+m->m_len, n->m_data, n->m_len);
    m->m_len += n->m_len;

    m_free(pData, n);
}


/* make m size bytes large */
void
m_inc(struct mbuf *m, int size)
{
    int datasize;

    /* some compiles throw up on gotos.  This one we can fake. */
    if (m->m_size > size)
        return;

    if (m->m_flags & M_EXT)
    {
        datasize = m->m_data - m->m_ext;
        m->m_ext = (char *)RTMemRealloc(m->m_ext, size);
#if 0
        if (m->m_ext == NULL)
            return (struct mbuf *)NULL;
#endif
        m->m_data = m->m_ext + datasize;
    }
    else
    {
        char *dat;
        datasize = m->m_data - m->m_dat;
        dat = (char *)RTMemAlloc(size);
#if 0
        if (dat == NULL)
            return (struct mbuf *)NULL;
#endif
        memcpy(dat, m->m_dat, m->m_size);

        m->m_ext = dat;
        m->m_data = m->m_ext + datasize;
        m->m_flags |= M_EXT;
    }

    m->m_size = size;
}


void
m_adj(struct mbuf *m, int len)
{
    if (m == NULL)
        return;
    if (len >= 0)
    {
        /* Trim from head */
        m->m_data += len;
        m->m_len -= len;
    }
    else
    {
        /* Trim from tail */
        len = -len;
        m->m_len -= len;
    }
    Assert(m->m_len >= 0);
}


/*
 * Copy len bytes from m, starting off bytes into n
 */
int
m_copy(struct mbuf *n, struct mbuf *m, int off, int len)
{
    if (len > M_FREEROOM(n))
        return -1;

    memcpy((n->m_data + n->m_len), (m->m_data + off), len);
    n->m_len += len;
    return 0;
}


/*
 * Given a pointer into an mbuf, return the mbuf
 * XXX This is a kludge, I should eliminate the need for it
 * Fortunately, it's not used often
 */
struct mbuf *
dtom(PNATState pData, void *dat)
{
    struct mbuf *m;

    DEBUG_CALL("dtom");
    DEBUG_ARG("dat = %lx", (long )dat);

    /* bug corrected for M_EXT buffers */
    for (m = m_usedlist.m_next; m != &m_usedlist; m = m->m_next)
    {
        if (m->m_flags & M_EXT)
        {
            if (   (char *)dat >=  m->m_ext
                && (char *)dat <  (m->m_ext + m->m_size))
                return m;
        }
        else
        {
            if (   (char *)dat >=  m->m_dat 
                && (char *)dat <  (m->m_dat + m->m_size))
                return m;
        }
    }

    DEBUG_ERROR((dfd, "dtom failed"));

    return (struct mbuf *)0;
}
