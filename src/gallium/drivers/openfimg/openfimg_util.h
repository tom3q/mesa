/*
 * Copyright (C) 2013 Tomasz Figa <tomasz.figa@gmail.com>
 *
 * Parts shamelessly copied from Freedreno driver:
 *
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef OPENFIMG_UTIL_H_
#define OPENFIMG_UTIL_H_

#include <freedreno_drmif.h>
#include <freedreno_ringbuffer.h>

#include "pipe/p_format.h"
#include "pipe/p_state.h"
#include "util/u_debug.h"
#include "util/u_double_list.h"
#include "util/u_math.h"

#include "fimg_3dse.xml.h"

#define OF_DBG_MSGS		0x1
#define OF_DBG_DISASM		0x2
#define OF_DBG_DCLEAR		0x4
#define OF_DBG_DGMEM		0x8
#define OF_DBG_VMSGS		0x10
#define OF_DBG_SHADER_OVERRIDE	0x20
extern int of_mesa_debug;

#define FORCE_DEBUG

#ifdef FORCE_DEBUG
#include <stdio.h>
#undef debug_printf
#define debug_printf _debug_printf
#endif

#define DBG_PRINT(mask, fmt, ...) \
		do { if (of_mesa_debug & mask) \
			debug_printf("%s:%d: "fmt "\n", \
				__FUNCTION__, __LINE__, __VA_ARGS__); } while (0)

#define DBG(...)	DBG_PRINT(OF_DBG_MSGS, __VA_ARGS__, "")
#define VDBG(...)	DBG_PRINT(OF_DBG_VMSGS, __VA_ARGS__, "")

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define ROUND_UP(val, to)	(((val) + (to) - 1) & ~((to) - 1))

#define min(a, b)	((a) < (b) ? (a) : (b))
#define max(a, b)	((a) > (b) ? (a) : (b))

#define swap(a, b, type) \
		do { \
			type __tmp = a; \
			\
			a = b; \
			b = __tmp; \
		} while (0)

#define CBUF_ADDR_32(buf, offs)	\
			((const uint32_t *)((const uint8_t *)(buf) + (offs)))
#define CBUF_ADDR_16(buf, offs)	\
			((const uint16_t *)((const uint8_t *)(buf) + (offs)))
#define CBUF_ADDR_8(buf, offs)	\
			((const uint8_t *)(buf) + (offs))

#define BUF_ADDR_32(buf, offs)	\
			((uint32_t *)((uint8_t *)(buf) + (offs)))
#define BUF_ADDR_16(buf, offs)	\
			((uint16_t *)((uint8_t *)(buf) + (offs)))
#define BUF_ADDR_8(buf, offs)	\
			((uint8_t *)(buf) + (offs))

/* for conditionally setting boolean flag(s): */
#define COND(bool, val) ((bool) ? (val) : 0)

#define LOG_DWORDS 0

#define MAX_MIP_LEVELS 12

enum of_request_type {
	G3D_REQUEST_REGISTER_WRITE = 0,
	G3D_REQUEST_SHADER_PROGRAM = 1,
	G3D_REQUEST_SHADER_DATA = 2,
	G3D_REQUEST_TEXTURE = 3,
#define G3D_TEXTURE_DIRTY	(1 << 0)
#define G3D_TEXTURE_DETACH	(1 << 1)
	G3D_REQUEST_COLORBUFFER = 4,
#define G3D_CBUFFER_DIRTY	(1 << 0)
#define G3D_CBUFFER_DETACH	(1 << 1)
	G3D_REQUEST_DEPTHBUFFER = 5,
#define G3D_DBUFFER_DIRTY	(1 << 0)
#define G3D_DBUFFER_DETACH	(1 << 1)
	G3D_REQUEST_DRAW = 6,
#define	G3D_DRAW_INDEXED	(1 << 31)
	G3D_REQUEST_VERTEX_BUFFER = 7,

	G3D_REQUEST_VTX_TEXTURE = -1,
};



enum fgtu_tex_format of_pipe2texture(enum pipe_format format);
enum fgpf_color_mode of_pipe2color(enum pipe_format format);
int of_depth_supported(enum pipe_format format);
uint32_t of_tex_swiz(enum pipe_format format, unsigned swizzle_r,
		unsigned swizzle_g, unsigned swizzle_b, unsigned swizzle_a);
enum fgpf_blend_factor of_blend_factor(unsigned factor);
enum fgpf_blend_op of_blend_func(unsigned func);
enum fgpf_stencil_action of_stencil_op(unsigned op);
enum fgpf_logical_op of_logic_op(unsigned op);
enum fgra_bfcull_face of_cull_face(unsigned face);
enum fgpf_test_mode of_test_mode(unsigned mode);
enum fgpf_stencil_mode of_stencil_mode(unsigned mode);

uint32_t of_hash_add(uint32_t hash, const void *data, size_t size);
uint32_t of_hash_finish(uint32_t hash);

static inline uint32_t of_hash_oneshot(const void *data, size_t size)
{
	return of_hash_finish(of_hash_add(0, data, size));
}

/* convert x,y to dword */
static inline uint32_t xy2d(uint16_t x, uint16_t y)
{
	return ((y & 0x3fff) << 16) | (x & 0x3fff);
}

static inline void
OUT_RING(struct fd_ringbuffer *ring, uint32_t data)
{
	if (LOG_DWORDS) {
		DBG("ring[%p]: OUT_RING   %04x:  %08x", ring,
				(uint32_t)(ring->cur - ring->last_start), data);
	}
	*(ring->cur++) = data;
}

static inline uint32_t *
OUT_PKT(struct fd_ringbuffer *ring, uint8_t opcode)
{
	uint32_t *pkt = ring->cur;
	uint32_t val = opcode << 24;
#ifdef DEBUG
	val |= 0xfa11ed;
#endif
	OUT_RING(ring, val);

	return pkt;
}

static inline void
END_PKT(struct fd_ringbuffer *ring, uint32_t *pkt)
{
	assert(pkt >= ring->last_start && pkt < ring->cur);
#ifdef DEBUG
	assert((*pkt & 0xffffff) == 0xfa11ed);
	*pkt &= ~0xffffff;
#endif
	*pkt |= ((ring->cur - pkt) - 1);
}

static inline enum pipe_format
pipe_surface_format(struct pipe_surface *psurf)
{
	if (!psurf)
		return PIPE_FORMAT_NONE;
	return psurf->format;
}

/*
 * Private memory heap
 */

struct of_heap;

struct of_heap *of_heap_create(void);
void of_heap_destroy(struct of_heap *heap);
void *of_heap_alloc(struct of_heap *heap, int sz);

/*
 * Bitmap helpers
 */

#define OF_BITMAP_BITS_PER_WORD		(8 * sizeof(uint32_t))
#define OF_BITMAP_WORDS_FOR_BITS(bits)	\
	(((bits) + OF_BITMAP_BITS_PER_WORD + 1) / OF_BITMAP_BITS_PER_WORD)

unsigned of_bitmap_find_next_set(uint32_t *words, unsigned size,
				 unsigned index);

static INLINE unsigned of_bitmap_find_first_set(uint32_t *words, unsigned size)
{
	return of_bitmap_find_next_set(words, size, 0);
}

#define OF_BITMAP_FOR_EACH_SET_BIT(bit, words, size)			\
	for (bit = of_bitmap_find_first_set(words, size);		\
	     bit != -1U;						\
	     bit = of_bitmap_find_next_set(words, size, bit + 1))

static INLINE unsigned of_bitmap_get(const uint32_t *words, unsigned index)
{
	unsigned word = index / OF_BITMAP_BITS_PER_WORD;
	unsigned bit  = index % OF_BITMAP_BITS_PER_WORD;

	return (words[word] >> bit) & 1;
}

static INLINE void of_bitmap_set(uint32_t *words, unsigned index)
{
	unsigned word = index / OF_BITMAP_BITS_PER_WORD;
	unsigned bit  = index % OF_BITMAP_BITS_PER_WORD;

	words[word] |= (1 << bit);
}

static INLINE void of_bitmap_clear(uint32_t *words, unsigned index)
{
	unsigned word = index / OF_BITMAP_BITS_PER_WORD;
	unsigned bit  = index % OF_BITMAP_BITS_PER_WORD;

	words[word] &= ~(1 << bit);
}

static INLINE void of_bitmap_or(uint32_t *dst, const uint32_t *src1,
				const uint32_t *src2, unsigned size)
{
	unsigned num_words = OF_BITMAP_WORDS_FOR_BITS(size);

	while (num_words--)
		*(dst++) = *(src1++) | *(src2++);
}

/*
 * Simple growing stack
 */

struct of_stack {
	void *buffer;
	unsigned element_size;
	unsigned initial_size;
	unsigned size;
	unsigned ptr;
};

void of_stack_grow(struct of_stack *stack);
struct of_stack *of_stack_create(unsigned element_size, unsigned initial_size);
void of_stack_destroy(struct of_stack *stack);

static INLINE void *
of_stack_push(struct of_stack *stack)
{
	stack->ptr += stack->element_size;
	if (stack->ptr > stack->size)
		of_stack_grow(stack);

	return stack->buffer + stack->ptr;
}

static INLINE void *
of_stack_pop(struct of_stack *stack)
{
	stack->ptr -= stack->element_size;
	return stack->buffer + stack->ptr;
}

static INLINE void *
of_stack_top(struct of_stack *stack)
{
	return stack->buffer + stack->ptr;
}

/*
 * List helpers
 */

static inline struct list_head *
list_head(struct list_head *head)
{
	return head->next;
}

static inline struct list_head *
list_tail(struct list_head *head)
{
	return head->prev;
}

static inline struct list_head *
list_pop(struct list_head *stack)
{
	struct list_head *item = list_head(stack);

	list_del(item);

	return item;
}

static inline void
list_push(struct list_head *item, struct list_head *stack)
{
	list_add(item, stack);
}

/**
 * list_is_singular - tests whether a list has just one entry.
 * @head: the list to test.
 */
static inline int
list_is_singular(const struct list_head *head)
{
	return !LIST_IS_EMPTY(head) && (head->next == head->prev);
}

static inline void
__list_cut_position(struct list_head *list, struct list_head *head,
		    struct list_head *entry)
{
	struct list_head *new_first = entry->next;
	list->next = head->next;
	list->next->prev = list;
	list->prev = entry;
	entry->next = list;
	head->next = new_first;
	new_first->prev = head;
}

/**
 * list_cut_position - cut a list into two
 * @list: a new list to add all removed entries
 * @head: a list with entries
 * @entry: an entry within head, could be the head itself
 *	and if so we won't cut the list
 *
 * This helper moves the initial part of @head, up to and
 * including @entry, from @head to @list. You should
 * pass on @entry an element you know is on @head. @list
 * should be an empty list or a list you do not care about
 * losing its data.
 *
 */
static inline void
list_cut_position(struct list_head *list, struct list_head *head,
		  struct list_head *entry)
{
	if (LIST_IS_EMPTY(head))
		return;
	if (list_is_singular(head) &&
		(head->next != entry && head != entry))
		return;
	if (entry == head)
		list_inithead(list);
	else
		__list_cut_position(list, head, entry);
}

static inline void __list_splice(const struct list_head *list,
				 struct list_head *prev,
				 struct list_head *next)
{
	struct list_head *first = list->next;
	struct list_head *last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

/**
 * list_splice - join two lists, this is designed for stacks
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void list_splice(const struct list_head *list,
				struct list_head *head)
{
	if (!LIST_IS_EMPTY(list))
		__list_splice(list, head, head->next);
}

/**
 * list_splice_tail - join two lists, each list being a queue
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void list_splice_tail(struct list_head *list,
				struct list_head *head)
{
	if (!LIST_IS_EMPTY(list))
		__list_splice(list, head->prev, head);
}

#endif /* OPENFIMG_UTIL_H_ */
