/*
 * lo_list.h
 *
 * Description: simple double link list used all over
 *
 * @author  bharrosh
 * @version 5.5
 *
 */
#ifndef __LO_LIST_H__
#define __LO_LIST_H__

#ifndef __LO_LIST_HEAD_DEFINED
struct lo_list_head {
	struct lo_list_head *next, *prev;
};
#define __LO_LIST_HEAD_DEFINED
#endif

static inline void lo_list_init(struct lo_list_head *list)
{
	list->next = list;
	list->prev = list;
}

static inline void _lo_list_add(struct lo_list_head *new,
			      struct lo_list_head *prev,
			      struct lo_list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}
static inline void lo_list_add(struct lo_list_head *new, struct lo_list_head *head)
{
	_lo_list_add(new, head->prev, head);
}

static inline void lo_list_del(struct lo_list_head *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}
static inline void lo_list_del_init(struct lo_list_head *entry)
{
	lo_list_del(entry);
	lo_list_init(entry);
}
static inline int lo_list_empty(const struct lo_list_head *head)
{
	return  (head->next == NULL) | (head->next == head);
}

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({			\
	typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#define lo_list_for_each_entry(pos, head, member)				\
	for (pos = container_of((head)->next, typeof(*pos), member);	\
	     &pos->member != (head); 	\
	     pos = container_of(pos->member.next, typeof(*pos), member))

#define lo_list_for_each_entry_safe(pos, t, head, member)			\
	for (pos = container_of((head)->next, typeof(*pos), member),		\
		t = container_of(pos->member.next, typeof(*pos), member);	\
	     &pos->member != (head); 						\
	     pos = t, t = container_of(t->member.next, typeof(*t), member))

#endif /* ndef __LO_LIST_H__ */
