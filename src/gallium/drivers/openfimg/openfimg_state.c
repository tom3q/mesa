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

#include "pipe/p_state.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_helpers.h"
#include "util/u_pack_color.h"

#include "openfimg_state.h"
#include "openfimg_context.h"
#include "openfimg_resource.h"
#include "openfimg_texture.h"
#include "openfimg_util.h"
#include "openfimg_vertex.h"
#include "openfimg_draw.h"

/* All the generic state handling.. In case of CSO's that are specific
 * to the GPU version, when the bind and the delete are common they can
 * go in here.
 */

static void
of_set_blend_color(struct pipe_context *pctx,
		const struct pipe_blend_color *blend_color)
{
	union util_color uc;
	struct of_context *ctx = of_context(pctx);

	util_pack_color(blend_color->color, PIPE_FORMAT_ABGR8888_UNORM, &uc);
	ctx->blend_color = uc.ui[0];
	ctx->dirty |= OF_DIRTY_BLEND_COLOR;
}

static void
of_set_stencil_ref(struct pipe_context *pctx,
		const struct pipe_stencil_ref *stencil_ref)
{
	struct of_context *ctx = of_context(pctx);
	ctx->stencil_ref =* stencil_ref;
	ctx->dirty |= OF_DIRTY_STENCIL_REF;
}

static void
of_set_clip_state(struct pipe_context *pctx,
		const struct pipe_clip_state *clip)
{
	DBG("TODO: ");
}

static void
of_set_sample_mask(struct pipe_context *pctx, unsigned sample_mask)
{
	struct of_context *ctx = of_context(pctx);
	ctx->sample_mask = (uint16_t)sample_mask;
	ctx->dirty |= OF_DIRTY_SAMPLE_MASK;
}

/* notes from calim on #dri-devel:
 * index==0 will be non-UBO (ie. glUniformXYZ()) all packed together padded
 * out to vec4's
 * I should be able to consider that I own the user_ptr until the next
 * set_constant_buffer() call, at which point I don't really care about the
 * previous values.
 * index>0 will be UBO's.. well, I'll worry about that later
 */
static void
of_set_constant_buffer(struct pipe_context *pctx, uint shader, uint index,
		struct pipe_constant_buffer *cb)
{
	struct of_context *ctx = of_context(pctx);
	struct of_constbuf_stateobj *so = &ctx->constbuf[shader];

	/* Note that the state tracker can unbind constant buffers by
	 * passing NULL here.
	 */
	if (unlikely(!cb)) {
		so->enabled_mask &= ~(1 << index);
		so->dirty_mask &= ~(1 << index);
		pipe_resource_reference(&so->cb[index].buffer, NULL);
		return;
	}

	pipe_resource_reference(&so->cb[index].buffer, cb->buffer);
	so->cb[index].buffer_offset = cb->buffer_offset;
	so->cb[index].buffer_size   = cb->buffer_size;
	so->cb[index].user_buffer   = cb->user_buffer;

	so->enabled_mask |= 1 << index;
	so->dirty_mask |= 1 << index;
	ctx->dirty |= OF_DIRTY_CONSTBUF;
}

static void
of_set_framebuffer_state(struct pipe_context *pctx,
		const struct pipe_framebuffer_state *framebuffer)
{
	struct of_context *ctx = of_context(pctx);
	struct of_framebuffer_stateobj *cso = &ctx->framebuffer;
	unsigned fmt = 0;

	DBG("%d: cbufs[0]=%p, zsbuf=%p", ctx->needs_flush,
			framebuffer->cbufs[0], framebuffer->zsbuf);

	of_context_render(pctx);

	util_copy_framebuffer_state(&cso->base, framebuffer);

	if (cso->base.cbufs[0])
		fmt = cso->base.cbufs[0]->format;
	cso->fgpf_fbctl = FGPF_FBCTL_COLOR_MODE(of_pipe2color(fmt));

	ctx->dirty |= OF_DIRTY_FRAMEBUFFER;

	ctx->disabled_scissor.minx = 0;
	ctx->disabled_scissor.miny = 0;
	ctx->disabled_scissor.maxx = cso->base.width;
	ctx->disabled_scissor.maxy = cso->base.height;

	ctx->dirty |= OF_DIRTY_SCISSOR;
}

static void
of_set_polygon_stipple(struct pipe_context *pctx,
		const struct pipe_poly_stipple *stipple)
{
	struct of_context *ctx = of_context(pctx);
	ctx->stipple = *stipple;
	ctx->dirty |= OF_DIRTY_STIPPLE;
}

static void
of_set_scissor_states(struct pipe_context *pctx,
		unsigned start_slot,
		unsigned num_scissors,
		const struct pipe_scissor_state *scissor)
{
	struct of_context *ctx = of_context(pctx);

	ctx->scissor = *scissor;
	ctx->dirty |= OF_DIRTY_SCISSOR;
}

static void
of_set_viewport_states(struct pipe_context *pctx,
		unsigned start_slot,
		unsigned num_viewports,
		const struct pipe_viewport_state *viewport)
{
	struct of_context *ctx = of_context(pctx);
	ctx->viewport = *viewport;
	ctx->dirty |= OF_DIRTY_VIEWPORT;
}

static void
of_set_vertex_buffers(struct pipe_context *pctx,
		unsigned start_slot, unsigned count,
		const struct pipe_vertex_buffer *vb)
{
	struct of_context *ctx = of_context(pctx);
	struct of_vertexbuf_stateobj *so = &ctx->vertexbuf;

	util_set_vertex_buffers_mask(so->vb, &so->enabled_mask, vb, start_slot, count);
	so->count = util_last_bit(so->enabled_mask);

	ctx->dirty |= OF_DIRTY_VTXBUF;
}

static void
of_set_index_buffer(struct pipe_context *pctx,
		const struct pipe_index_buffer *ib)
{
	struct of_context *ctx = of_context(pctx);

	if (ib) {
		pipe_resource_reference(&ctx->indexbuf.buffer, ib->buffer);
		ctx->indexbuf.index_size = ib->index_size;
		ctx->indexbuf.offset = ib->offset;
		ctx->indexbuf.user_buffer = ib->user_buffer;
	} else {
		pipe_resource_reference(&ctx->indexbuf.buffer, NULL);
	}

	ctx->dirty |= OF_DIRTY_INDEXBUF;
}

static void *
of_blend_state_create(struct pipe_context *pctx,
		const struct pipe_blend_state *cso)
{
	const struct pipe_rt_blend_state *rt = cso->rt;
	struct of_blend_stateobj *so;

	if (cso->independent_blend_enable) {
		DBG("Unsupported! independent blend state");
		return NULL;
	}

	so = CALLOC_STRUCT(of_blend_stateobj);
	if (!so)
		return NULL;

	so->base = *cso;

	so->fgpf_blend =
		FGPF_BLEND_COLOR_SRC_FUNC(of_blend_factor(rt->rgb_src_factor)) |
		FGPF_BLEND_COLOR_EQUATION(of_blend_func(rt->rgb_func)) |
		FGPF_BLEND_COLOR_DST_FUNC(of_blend_factor(rt->rgb_dst_factor)) |
		FGPF_BLEND_ALPHA_SRC_FUNC(of_blend_factor(rt->alpha_src_factor)) |
		FGPF_BLEND_ALPHA_EQUATION(of_blend_func(rt->alpha_func)) |
		FGPF_BLEND_ALPHA_DST_FUNC(of_blend_factor(rt->alpha_dst_factor));

	if (rt->blend_enable)
		so->fgpf_blend |= FGPF_BLEND_ENABLE;

	so->fgpf_logop =
		FGPF_LOGOP_COLOR_OP(of_logic_op(cso->logicop_func)) |
		FGPF_LOGOP_ALPHA_OP(of_logic_op(cso->logicop_func));

	if (cso->logicop_enable)
		so->fgpf_logop |= FGPF_LOGOP_ENABLE;

	if (!(rt->colormask & PIPE_MASK_R))
		so->fgpf_cbmsk |= FGPF_CBMSK_RED;
	if (!(rt->colormask & PIPE_MASK_G))
		so->fgpf_cbmsk |= FGPF_CBMSK_GREEN;
	if (!(rt->colormask & PIPE_MASK_B))
		so->fgpf_cbmsk |= FGPF_CBMSK_BLUE;
	if (!(rt->colormask & PIPE_MASK_A))
		so->fgpf_cbmsk |= FGPF_CBMSK_ALPHA;

	so->fgpf_fbctl = FGPF_FBCTL_ALPHA_CONST(0xff)
				| FGPF_FBCTL_ALPHA_THRESHOLD(0x80);

	if (cso->dither)
		so->fgpf_fbctl |= FGPF_FBCTL_DITHER_ON;
	if (cso->alpha_to_one)
		so->fgpf_fbctl |= FGPF_FBCTL_OPAQUE_ALPHA;

	return so;
}

static void
of_blend_state_bind(struct pipe_context *pctx, void *hwcso)
{
	OF_CSO_BIND(pctx, blend, OF_DIRTY_BLEND, hwcso);
}

static void
of_blend_state_delete(struct pipe_context *pctx, void *hwcso)
{
	FREE(hwcso);
}

static void *
of_rasterizer_state_create(struct pipe_context *pctx,
		const struct pipe_rasterizer_state *cso)
{
	struct of_rasterizer_stateobj *so;
	float psize_min, psize_max;

	so = CALLOC_STRUCT(of_rasterizer_stateobj);
	if (!so)
		return NULL;

	if (cso->point_size_per_vertex) {
		psize_min = util_get_min_point_size(cso);
		psize_max = 2048;
	} else {
		/* Force the point size to be as if the vertex output was disabled. */
		psize_min = cso->point_size;
		psize_max = cso->point_size;
	}

	so->base = *cso;

	if (cso->cull_face) {
		so->fgra_bfcull =
				FGRA_BFCULL_FACE(of_cull_face(cso->cull_face));
		so->fgra_bfcull |= FGRA_BFCULL_ENABLE;
		if (!cso->front_ccw)
			so->fgra_bfcull |= FGRA_BFCULL_FRONT_CW;
	}

	so->fgra_psize_min = FGRA_PSIZE_MIN(psize_min);
	so->fgra_psize_max = FGRA_PSIZE_MAX(psize_max);

	return so;
}

static void
of_rasterizer_state_bind(struct pipe_context *pctx, void *hwcso)
{
	OF_CSO_BIND(pctx, rasterizer, OF_DIRTY_RASTERIZER, hwcso);
}

static void
of_rasterizer_state_delete(struct pipe_context *pctx, void *hwcso)
{
	FREE(hwcso);
}

static void *
of_zsa_state_create(struct pipe_context *pctx,
		const struct pipe_depth_stencil_alpha_state *cso)
{
	struct of_zsa_stateobj *so;

	so = CALLOC_STRUCT(of_zsa_stateobj);
	if (!so)
		return NULL;

	so->base = *cso;

	if (cso->depth.enabled) {
		so->fgpf_deptht =
			FGPF_DEPTHT_ENABLE |
			FGPF_DEPTHT_MODE(of_test_mode(cso->depth.func));
	}

	if (!cso->depth.writemask)
		so->fgpf_dbmsk |= FGPF_DBMSK_DEPTH_MASK;

	if (cso->stencil[0].enabled) {
		const struct pipe_stencil_state *s = &cso->stencil[0];

		so->fgpf_frontst =
			FGPF_FRONTST_ENABLE |
			FGPF_FRONTST_MODE(of_stencil_mode(s->func)) |
			FGPF_FRONTST_MASK(s->valuemask) |
			FGPF_FRONTST_SFAIL(of_stencil_op(s->fail_op)) |
			FGPF_FRONTST_DPPASS(of_stencil_op(s->zpass_op)) |
			FGPF_FRONTST_DPFAIL(of_stencil_op(s->zfail_op));

		so->fgpf_dbmsk |= FGPF_DBMSK_FRONT_STENCIL_MASK(~s->writemask);

		if (cso->stencil[1].enabled)
			s = &cso->stencil[1];

		so->fgpf_backst =
			FGPF_FRONTST_MODE(of_stencil_mode(s->func)) |
			FGPF_FRONTST_MASK(s->valuemask) |
			FGPF_FRONTST_SFAIL(of_stencil_op(s->fail_op)) |
			FGPF_FRONTST_DPPASS(of_stencil_op(s->zpass_op)) |
			FGPF_FRONTST_DPFAIL(of_stencil_op(s->zfail_op));

		so->fgpf_dbmsk |= FGPF_DBMSK_BACK_STENCIL_MASK(~s->writemask);
	}

	if (cso->alpha.enabled)
		so->fgpf_alphat =
			FGPF_ALPHAT_ENABLE |
			FGPF_ALPHAT_MODE(of_test_mode(cso->alpha.func)) |
			FGPF_ALPHAT_VALUE(float_to_ubyte(cso->alpha.ref_value));

	return so;
}

static void
of_zsa_state_bind(struct pipe_context *pctx, void *hwcso)
{
	OF_CSO_BIND(pctx, zsa, OF_DIRTY_ZSA, hwcso);
}

static void
of_zsa_state_delete(struct pipe_context *pctx, void *hwcso)
{
	FREE(hwcso);
}

struct of_element_data {
	uint16_t offset;
	uint8_t transfer_index;
	uint8_t width;
};

static void
of_allocate_vertex_buffer(struct of_context *ctx, struct of_vertex_stateobj *so,
			  struct of_element_data *elems)
{
	struct of_vertex_transfer *transfer;
	unsigned batch_size;
	unsigned size;
	unsigned sum;
	unsigned i;

	sum = 0;

	transfer = so->transfers;
	for (i = 0; i < so->num_transfers; ++i, ++transfer)
		sum += ROUND_UP(transfer->width, 4);

	batch_size = VERTEX_BUFFER_SIZE / sum;

	for (; batch_size > 0; --batch_size) {
		size = 0;
		transfer = so->transfers;
		for (i = 0; i < so->num_transfers; ++i, ++transfer) {
			transfer->offset = size;
			size += ROUND_UP(batch_size
					* ROUND_UP(transfer->width, 4), 32);
		}
		if (size <= VERTEX_BUFFER_SIZE)
			break;
	}

	so->batch_size = batch_size;

	for (i = 0; i < so->num_elements; ++i) {
		struct of_element_data *elem = &elems[i];
		struct of_vertex_element *element = &so->elements[i];
		struct of_vertex_transfer *transfer =
					&so->transfers[elem->transfer_index];
		unsigned offset = transfer->offset + elem->offset;
		unsigned stride = ROUND_UP(transfer->width, 4);

		element->vbctrl = FGHI_ATTRIB_VBCTRL_STRIDE(stride)
					| FGHI_ATTRIB_VBCTRL_RANGE(0xffff);
		element->vbbase = FGHI_ATTRIB_VBBASE_ADDR(offset);
	}
}

static void
of_vtx_format(struct of_vertex_element *element, enum pipe_format fmt,
	      struct of_element_data *elem)
{
	const struct util_format_description *desc;
	int first_comp;
	enum fghi_attrib_dt type;

	desc = util_format_description(fmt);
	if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
		goto out_unknown;

	first_comp = util_format_get_first_non_void_channel(fmt);
	if (first_comp < 0)
		goto out_unknown;

	if (desc->is_mixed)
		goto out_unknown;

	switch (desc->channel[first_comp].type) {
	case UTIL_FORMAT_TYPE_FLOAT:
		switch (desc->channel[first_comp].size) {
		case 16:
			type = DT_HFLOAT;
			break;
		case 32:
			type = DT_FLOAT;
			break;
		default:
			goto out_unknown;
		}
		break;
	case UTIL_FORMAT_TYPE_FIXED:
		if (desc->channel[first_comp].size != 32)
			goto out_unknown;
		if (desc->channel[first_comp].normalized)
			type = DT_NFIXED;
		else
			type = DT_FIXED;
		break;
	case UTIL_FORMAT_TYPE_SIGNED:
		switch (desc->channel[first_comp].size) {
		case 8:
			type = DT_BYTE;
			break;
		case 16:
			type = DT_SHORT;
			break;
		case 32:
			type = DT_INT;
			break;
		default:
			goto out_unknown;
		}
		if (desc->channel[first_comp].normalized)
			type += DT_NBYTE;
		break;
	case UTIL_FORMAT_TYPE_UNSIGNED:
		switch (desc->channel[first_comp].size) {
		case 8:
			type = DT_UBYTE;
			break;
		case 16:
			type = DT_USHORT;
			break;
		case 32:
			type = DT_UINT;
			break;
		default:
			goto out_unknown;
		}
		if (desc->channel[first_comp].normalized)
			type += DT_NBYTE;
		break;
	default:
		goto out_unknown;
	}

	element->attrib = FGHI_ATTRIB_DT(type) |
			FGHI_ATTRIB_NUM_COMP(desc->nr_channels - 1) |
			FGHI_ATTRIB_SRCX(0) | FGHI_ATTRIB_SRCY(1) |
			FGHI_ATTRIB_SRCZ(2) | FGHI_ATTRIB_SRCW(3);
	elem->width = desc->block.bits / 8;

	return;

out_unknown:
	DBG("unsupported vertex format %s\n", util_format_name(fmt));
}

static int
array_compare(const void *e1, const void *e2)
{
	const struct pipe_vertex_element *elem1, *elem2;

	elem1 = *(const struct pipe_vertex_element **)e1;
	elem2 = *(const struct pipe_vertex_element **)e2;

	if (elem1->vertex_buffer_index != elem2->vertex_buffer_index)
		return elem1->vertex_buffer_index - elem2->vertex_buffer_index;

	return elem1->src_offset - elem2->src_offset;
}

static void *
of_vertex_state_create(struct pipe_context *pctx, unsigned num_elements,
		       const struct pipe_vertex_element *elements)
{
	struct of_context *ctx = of_context(pctx);
	struct of_vertex_transfer *transfer;
	struct of_vertex_stateobj *so;
	struct of_element_data elems[OF_MAX_ATTRIBS];
	const struct pipe_vertex_element *arrays[OF_MAX_ATTRIBS];
	int i;

	if (num_elements < 1 || num_elements >= OF_MAX_ATTRIBS)
		return NULL;

	so = CALLOC_STRUCT(of_vertex_stateobj);
	if (!so)
		return NULL;

	LIST_INITHEAD(&so->vtx_inv_list);

	memcpy(so->pipe, elements, sizeof(*elements) * num_elements);
	so->num_elements = num_elements;

	for (i = 0; i < num_elements; ++i) {
		const struct pipe_vertex_element *draw_element = &elements[i];
		struct of_vertex_element *vtx_element = &so->elements[i];

		of_vtx_format(vtx_element, draw_element->src_format,
				&elems[i]);

		arrays[i] = &so->pipe[i];
	}

	/* Mark last element with terminating flag */
	so->elements[num_elements - 1].attrib |= FGHI_ATTRIB_LAST_ATTR;

	/* Try to detect interleaved arrays */
	qsort(arrays, num_elements, sizeof(*arrays), array_compare);

	transfer = so->transfers;
	for (i = 0; i < num_elements;) {
		const struct pipe_vertex_element *pipe = arrays[i];
		unsigned attrib = pipe - so->pipe;
		struct of_element_data *elem = &elems[attrib];

		if (!(so->vb_mask & (1 << pipe->vertex_buffer_index))) {
			so->vb_map[pipe->vertex_buffer_index] = so->num_vb++;
			so->vb_mask |= (1 << pipe->vertex_buffer_index);
		}
		transfer->vertex_buffer_index = pipe->vertex_buffer_index;
		transfer->src_offset = pipe->src_offset;
		transfer->width = elem->width;
		elem->offset = 0;
		elem->transfer_index = so->num_transfers;

		for (++i; i < num_elements; ++i) {
			const struct pipe_vertex_element *pipe2 = arrays[i];
			unsigned attrib2 = pipe2 - so->pipe;
			struct of_element_data *elem2 = &elems[attrib2];
			unsigned offset = pipe2->src_offset - pipe->src_offset;

			/* Interleaved arrays reside in one vertex buffer. */
			if (pipe->vertex_buffer_index
			    != pipe2->vertex_buffer_index)
				break;

			/*
			 * Interleaved arrays must be contiguous
			 * and attributes must be word-aligned.
			 */
			if (offset != ROUND_UP(transfer->width, 4)) {
				so->ugly = true; /* Needs repacking */
				break;
			}

			transfer->width = offset + elem2->width;
			elem2->offset = offset;
			elem2->transfer_index = so->num_transfers;
		}

		++transfer;
		++so->num_transfers;
	}

	of_allocate_vertex_buffer(ctx, so, elems);

	return so;
}

static void
of_vertex_state_delete(struct pipe_context *pctx, void *hwcso)
{
	struct of_context *ctx = of_context(pctx);
	struct of_vertex_stateobj *vtx = hwcso;

	of_invalidate_vtx_caches(ctx, vtx);
	FREE(hwcso);
}

static void
of_vertex_state_bind(struct pipe_context *pctx, void *hwcso)
{
	OF_CSO_BIND(pctx, vtx, OF_DIRTY_VTXSTATE, hwcso);
}

void
of_state_init(struct pipe_context *pctx)
{
	pctx->set_blend_color = of_set_blend_color;
	pctx->set_stencil_ref = of_set_stencil_ref;
	pctx->set_clip_state = of_set_clip_state;
	pctx->set_sample_mask = of_set_sample_mask;
	pctx->set_constant_buffer = of_set_constant_buffer;
	pctx->set_framebuffer_state = of_set_framebuffer_state;
	pctx->set_polygon_stipple = of_set_polygon_stipple;
	pctx->set_scissor_states = of_set_scissor_states;
	pctx->set_viewport_states = of_set_viewport_states;

	pctx->set_vertex_buffers = of_set_vertex_buffers;
	pctx->set_index_buffer = of_set_index_buffer;

	pctx->create_blend_state = of_blend_state_create;
	pctx->bind_blend_state = of_blend_state_bind;
	pctx->delete_blend_state = of_blend_state_delete;

	pctx->create_rasterizer_state = of_rasterizer_state_create;
	pctx->bind_rasterizer_state = of_rasterizer_state_bind;
	pctx->delete_rasterizer_state = of_rasterizer_state_delete;

	pctx->create_depth_stencil_alpha_state = of_zsa_state_create;
	pctx->bind_depth_stencil_alpha_state = of_zsa_state_bind;
	pctx->delete_depth_stencil_alpha_state = of_zsa_state_delete;

	pctx->create_vertex_elements_state = of_vertex_state_create;
	pctx->delete_vertex_elements_state = of_vertex_state_delete;
	pctx->bind_vertex_elements_state = of_vertex_state_bind;
}
