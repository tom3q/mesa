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
#include "util/u_clear.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_prim.h"
#include "util/u_format.h"
#include "util/u_surface.h"

#include "cso_cache/cso_cache.h"
#include "cso_cache/cso_hash.h"

#include "openfimg_draw.h"
#include "openfimg_emit.h"
#include "openfimg_context.h"
#include "openfimg_state.h"
#include "openfimg_program.h"
#include "openfimg_resource.h"
#include "openfimg_vertex.h"
#include "openfimg_util.h"

#include <stdlib.h>

static uint32_t
add_to_hash(uint32_t hash, const void *data, size_t size)
{
	const uint32_t *key = data;
	uint32_t i;

	for (i = 0; i < size / 4; ++i) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	return hash;
}

static uint32_t
finish_hash(uint32_t hash)
{
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash;
}

static INLINE unsigned
of_draw_hash(struct of_draw_info *req)
{
	uint32_t hash;

	hash = add_to_hash(0, &req->base, sizeof(req->base));
	hash = add_to_hash(hash, req->vb,
			req->base.num_vb * sizeof(req->vb[0]));
	if (req->base.info.indexed)
		hash = add_to_hash(hash, &req->ib, sizeof(req->ib));

	return finish_hash(hash);
}

static int
draw_info_compare(const void *a, const void *b, size_t size)
{
	const struct of_draw_info *req1 = a;
	const struct of_draw_info *req2 = b;
	int ret;

	ret = memcmp(&req1->base, &req2->base, sizeof(req1->base));
	if (ret)
		return ret;

	ret = memcmp(req1->vb, req2->vb,
			req1->base.num_vb * sizeof(req1->vb[0]));
	if (ret)
		return ret;

	if (req1->base.info.indexed)
		return memcmp(&req1->ib, &req2->ib, sizeof(req1->ib));

	return 0;
}

static void
of_primconvert_run(struct of_context *ctx, struct of_vertex_info *vertex)
{
	struct pipe_transfer *src_transfer = NULL, *dst_transfer = NULL;
	const struct of_draw_info *draw = &vertex->key;
	struct pipe_index_buffer *new_ib = &vertex->ib;
	const struct pipe_index_buffer *ib = &draw->ib;
	const struct pipe_draw_info *info = &draw->base.info;
	const void *src;
	void *dst;

	new_ib->buffer = pipe_buffer_create(ctx->base.screen,
						PIPE_BIND_INDEX_BUFFER,
						PIPE_USAGE_IMMUTABLE,
						new_ib->index_size
						* vertex->count);

	dst = pipe_buffer_map(&ctx->base, new_ib->buffer, PIPE_TRANSFER_WRITE,
				&dst_transfer);

	if (info->indexed) {
		src = ib->user_buffer;
		if (!src) {
			src = pipe_buffer_map(&ctx->base, ib->buffer,
						PIPE_TRANSFER_READ,
						&src_transfer);
		}
		vertex->trans_func(src, info->start, vertex->count, dst);
	}
	else {
		vertex->gen_func(info->start, vertex->count, dst);
	}

	if (src_transfer)
		pipe_buffer_unmap(&ctx->base, src_transfer);

	if (dst_transfer)
		pipe_buffer_unmap(&ctx->base, dst_transfer);
}

static void
of_primconvert_release(struct of_context *ctx, struct of_vertex_info *vertex)
{
	struct pipe_index_buffer *new_ib = &vertex->ib;

	pipe_resource_reference(&new_ib->buffer, NULL);
}

static unsigned int dummy_const;

static void
of_build_vertex_data(struct of_context *ctx, struct of_vertex_info *vertex)
{
	struct pipe_transfer *vb_transfer[OF_MAX_ATTRIBS];
	const struct pipe_index_buffer *ib = &vertex->ib;
	const struct of_draw_info *draw = &vertex->key;
	struct pipe_transfer *ib_transfer = NULL;
	const struct of_vertex_transfer *transfer;
	const void *vb_ptr[OF_MAX_ATTRIBS];
	struct of_vertex_data vdata;
	bool primconvert = false;
	const void *indices;
	unsigned i;

	if (!of_supported_prim(ctx, draw->base.info.mode)) {
		of_primconvert_run(ctx, vertex);
		primconvert = true;
	}

	memset(vb_ptr, 0, sizeof(vb_ptr));
	memset(vb_transfer, 0, sizeof(vb_transfer));

	transfer = draw->base.vtx->transfers;
	for (i = 0; i < draw->base.vtx->num_transfers; ++i, ++transfer) {
		unsigned buf_idx = transfer->vertex_buffer_index;
		const struct pipe_vertex_buffer *vb = &draw->vb[buf_idx];

		if (!vb_ptr[buf_idx]) {
			if (vb->user_buffer)
				vb_ptr[buf_idx] = vb->user_buffer;
			else
				vb_ptr[buf_idx] = pipe_buffer_map(&ctx->base,
							vb->buffer,
							PIPE_TRANSFER_READ,
							&vb_transfer[buf_idx]);
		}

		vdata.transfers[i] = vb_ptr[buf_idx] + transfer->src_offset;
	}

	VDBG("TODO");
	// TODO: Prepare constant elements
	vdata.const_data = &dummy_const;
	vdata.const_size = 0;

	vdata.ctx = ctx;
	vdata.info = vertex;

	if (!vertex->indexed) {
		of_prepare_draw_seq(&vdata);
	} else {
		if (ib->buffer)
			indices = pipe_buffer_map(&ctx->base, ib->buffer,
							PIPE_TRANSFER_READ,
							&ib_transfer);
		else
			indices = ib->user_buffer;

		switch (ib->index_size) {
		case 4:
			of_prepare_draw_idx32(&vdata, indices);
			break;
		case 2:
			of_prepare_draw_idx16(&vdata, indices);
			break;
		case 1:
			of_prepare_draw_idx8(&vdata, indices);
			break;
		default:
			assert(0);
		}

		if (ib_transfer)
			pipe_buffer_unmap(&ctx->base, ib_transfer);
	}

	if (primconvert)
		of_primconvert_release(ctx, vertex);

	transfer = draw->base.vtx->transfers;
	for (i = 0; i < draw->base.vtx->num_transfers; ++i, ++transfer) {
		unsigned buf_idx = transfer->vertex_buffer_index;

		if (!vb_ptr[buf_idx])
			continue;

		if (vb_transfer[buf_idx])
			pipe_buffer_unmap(&ctx->base, vb_transfer[buf_idx]);

		vb_ptr[buf_idx] = NULL;
	}
}

static void
of_primconvert_prepare(struct of_context *ctx, struct of_vertex_info *vertex)
{
	const struct pipe_rasterizer_state *rast = ctx->cso.rasterizer;
	const struct of_draw_info *draw = &vertex->key;
	struct pipe_index_buffer *new_ib = &vertex->ib;
	const struct pipe_draw_info *info = &draw->base.info;
	unsigned api_pv;

	memset(new_ib, 0, sizeof(*new_ib));
	vertex->indexed = true;

	api_pv = (rast->flatshade
			&& !rast->flatshade_first) ? PV_LAST : PV_FIRST;

	if (info->indexed)
		u_index_translator(ctx->primtype_mask,
					info->mode, draw->ib.index_size,
					info->count, api_pv, api_pv,
					&vertex->mode, &new_ib->index_size,
					&vertex->count, &vertex->trans_func);
	else
		u_index_generator(ctx->primtype_mask,
					info->mode, info->start, info->count,
					api_pv, api_pv, &vertex->mode,
					&new_ib->index_size, &vertex->count,
					&vertex->gen_func);
}

static struct of_vertex_info *of_create_vertex_info(struct of_context *ctx,
			const struct of_draw_info *draw, bool bypass_cache)
{
	struct of_vertex_info *vertex = CALLOC_STRUCT(of_vertex_info);

	if (vertex == NULL)
		return NULL;

	memcpy(&vertex->key, draw, sizeof(*draw));

	vertex->bypass_cache = bypass_cache;
	vertex->first_draw = true;

	/* emulate unsupported primitives: */
	if (of_supported_prim(ctx, draw->base.info.mode)) {
		vertex->indexed = draw->base.info.indexed;
		vertex->mode = draw->base.info.mode;
		vertex->count = draw->base.info.count;
		vertex->trans_func = NULL;
		vertex->gen_func = NULL;
		memcpy(&vertex->ib, &draw->ib, sizeof(vertex->ib));
	} else {
		of_primconvert_prepare(ctx, vertex);
	}

	vertex->draw_mode = ctx->primtypes[vertex->mode];

	of_build_vertex_data(ctx, vertex);

	return vertex;
}

static void
of_emit_draw(struct of_context *ctx, struct of_vertex_info *info,
	     uint32_t dirty)
{
	const struct of_draw_info *draw = &info->key;
	struct of_rasterizer_stateobj *rasterizer;
	struct fd_ringbuffer *ring = ctx->ring;
	struct of_vertex_buffer *buf, *tmp;
	uint32_t *pkt;
	unsigned i;

	rasterizer = of_rasterizer_stateobj(ctx->cso.rasterizer);

	if (dirty & (OF_DIRTY_VTXSTATE | OF_DIRTY_VTXBUF |
							OF_DIRTY_RASTERIZER)
	    || ctx->last_draw_mode != info->draw_mode) {
		pkt = OUT_PKT(ring, G3D_REQUEST_REGISTER_WRITE);

		for (i = 0; i < draw->base.vtx->num_elements; ++i) {
			const struct of_vertex_element *element =
						&draw->base.vtx->elements[i];

			OUT_RING(ring, REG_FGHI_ATTRIB(i));
			OUT_RING(ring, element->attrib);
			OUT_RING(ring, REG_FGHI_ATTRIB_VBCTRL(i));
			OUT_RING(ring, element->vbctrl);
			OUT_RING(ring, REG_FGHI_ATTRIB_VBBASE(i));
			OUT_RING(ring, element->vbbase);
		}

		OUT_RING(ring, REG_FGPE_VERTEX_CONTEXT);
		OUT_RING(ring, rasterizer->fgpe_vertex_context |
				FGPE_VERTEX_CONTEXT_TYPE(info->draw_mode) |
				FGPE_VERTEX_CONTEXT_VSOUT(8));

		END_PKT(ring, pkt);

		ctx->last_draw_mode = info->draw_mode;
		ctx->cso_active.vtx = ctx->cso.vtx;
	}

	LIST_FOR_EACH_ENTRY_SAFE(buf, tmp, &info->buffers, list) {
		pkt = OUT_PKT(ring, G3D_REQUEST_DRAW);
		OUT_RING(ring, buf->nr_vertices);
		OUT_RING(ring, buf->handle);
		OUT_RING(ring, 0);
		OUT_RING(ring, buf->bytes_used);
		END_PKT(ring, pkt);

		if (info->first_draw || info->bypass_cache) {
			LIST_DEL(&buf->list);
			of_put_batch_buffer(ctx, buf);
		}
	}

	info->first_draw = false;
}

static void
of_kill_draw_caches(struct of_context *ctx, struct pipe_resource *buf)
{
	DBG("TODO");
	assert(0);
}

static void
of_draw(struct of_context *ctx, const struct pipe_draw_info *info)
{
	struct of_draw_info *draw = ctx->draw;
	struct of_vertex_info *vertex = NULL;
	bool bypass_cache = false;
	bool index_dirty = false;
	unsigned dirty = 0;
	unsigned hash_key;
	unsigned i;
	unsigned dirty_state = ctx->dirty;

	if (draw->base.info.indexed != info->indexed
	    || dirty_state & OF_DIRTY_INDEXBUF) {
		struct pipe_index_buffer *indexbuf = &ctx->indexbuf;

		if (info->indexed)
			memcpy(&draw->ib, indexbuf, sizeof(draw->ib));
	}

	if (info->indexed) {
		if (!draw->ib.buffer)
			bypass_cache = true;
		else if (of_resource(draw->ib.buffer)->dirty)
			index_dirty = true;
	}

	if (dirty_state & (OF_DIRTY_VTXSTATE | OF_DIRTY_VTXBUF)) {
		struct of_vertexbuf_stateobj *vertexbuf = &ctx->vertexbuf;
		struct of_vertex_stateobj *vtx = ctx->cso.vtx;

		if (vtx->num_elements < 1
		    || vtx->num_elements >= OF_MAX_ATTRIBS)
			return;

		draw->base.vtx = ctx->cso.vtx;

		draw->base.num_vb = vertexbuf->count;
		memcpy(draw->vb, vertexbuf->vb,
			vertexbuf->count * sizeof(vertexbuf->vb[0]));
	}

	for (i = 0; i < draw->base.num_vb; ++i) {
		struct pipe_vertex_buffer *vb = &draw->vb[i];

		if (!vb->buffer)
			bypass_cache = true;
		else if (of_resource(vb->buffer)->dirty)
			dirty |= 1 << i;
	}

	memcpy(&draw->base.info, info, sizeof(draw->base.info));

	hash_key = of_draw_hash(draw);
	vertex = cso_hash_find_data_from_template_c(ctx->draw_hash, hash_key,
					draw, sizeof(*draw), draw_info_compare);

	if (!vertex) {
		vertex = of_create_vertex_info(ctx, draw, bypass_cache);
		cso_hash_insert(ctx->draw_hash, hash_key, vertex);
	} else if (dirty || index_dirty || LIST_IS_EMPTY(&vertex->buffers)) {
		while (dirty) {
			unsigned buffer = ffs(dirty) - 1;
			struct pipe_vertex_buffer *vb = &draw->vb[buffer];

			of_kill_draw_caches(ctx, vb->buffer);
			dirty &= ~(1 << buffer);
			of_resource(vb->buffer)->dirty = false;

			vertex->first_draw = true;
		}

		if (index_dirty) {
			of_kill_draw_caches(ctx, draw->ib.buffer);
			of_resource(draw->ib.buffer)->dirty = false;

			vertex->first_draw = true;
		}

		of_build_vertex_data(ctx, vertex);
	}

	of_emit_state(ctx, dirty_state);
	of_emit_draw(ctx, vertex, dirty_state);
}

static void
of_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info)
{
	struct of_context *ctx = of_context(pctx);
	struct pipe_framebuffer_state *pfb = &ctx->framebuffer.base;
	struct pipe_scissor_state *scissor = of_context_get_scissor(ctx);
	unsigned i, buffers = 0;

	/* if we supported transform feedback, we'd have to disable this: */
	if (((scissor->maxx - scissor->minx) *
			(scissor->maxy - scissor->miny)) == 0) {
		return;
	}

	ctx->needs_flush = true;

	/*
	 * Figure out the buffers/features we need:
	 */

	if (of_depth_enabled(ctx)) {
		buffers |= OF_BUFFER_DEPTH;
		of_resource(pfb->zsbuf->texture)->dirty = true;
	}

	if (of_stencil_enabled(ctx)) {
		buffers |= OF_BUFFER_STENCIL;
		of_resource(pfb->zsbuf->texture)->dirty = true;
	}

	for (i = 0; i < pfb->nr_cbufs; i++) {
		struct pipe_resource *surf;

		if (!pfb->cbufs[i])
			continue;

		surf = pfb->cbufs[i]->texture;

		of_resource(surf)->dirty = true;
		buffers |= OF_BUFFER_COLOR;
	}

	/* any buffers that haven't been cleared, we need to restore: */
	ctx->restore |= buffers & (OF_BUFFER_ALL & ~ctx->cleared);
	/* and any buffers used, need to be resolved: */
	ctx->resolve |= buffers;

	of_draw(ctx, info);
}

static void
of_clear(struct pipe_context *pctx, unsigned buffers,
		const union pipe_color_union *color, double depth, unsigned stencil)
{
	struct of_context *ctx = of_context(pctx);
	struct pipe_framebuffer_state *pfb = &ctx->framebuffer.base;
	struct fd_ringbuffer *ring = ctx->ring;
	uint32_t *pkt;

	if (!ctx->clear_vertex_info)
		of_context_init_solid(ctx);

	ctx->cleared |= buffers;
	ctx->resolve |= buffers;
	ctx->needs_flush = true;

	if (!pfb->cbufs[0])
		buffers &= ~PIPE_CLEAR_COLOR;

	if (!pfb->zsbuf)
		buffers &= ~(PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL);

	if (buffers & PIPE_CLEAR_COLOR)
		of_resource(pfb->cbufs[0]->texture)->dirty = true;

	if (buffers & (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL))
		of_resource(pfb->zsbuf->texture)->dirty = true;

	DBG("%x depth=%f, stencil=%u (%s/%s)", buffers, depth, stencil,
		util_format_short_name(pipe_surface_format(pfb->cbufs[0])),
		util_format_short_name(pipe_surface_format(pfb->zsbuf)));

	/* emit clear program */
	of_program_emit(ctx, ctx->solid_vp);
	of_program_emit(ctx, ctx->solid_fp);

	/* emit clear color */
	pkt = OUT_PKT(ring, G3D_REQUEST_SHADER_DATA);
	OUT_RING(ring, RSD_UNIT_TYPE_OFFS(SHADER_FRAGMENT,
			G3D_SHADER_DATA_FLOAT, 0));
	OUT_RING(ring, color->ui[0]);
	OUT_RING(ring, color->ui[1]);
	OUT_RING(ring, color->ui[2]);
	OUT_RING(ring, color->ui[3]);
	END_PKT(ring, pkt);

	/* emit applicable generic state */
	of_emit_state(ctx, ctx->dirty &
			(OF_DIRTY_BLEND | OF_DIRTY_VIEWPORT |
			OF_DIRTY_FRAMEBUFFER | OF_DIRTY_SCISSOR));

	/* emit clear-specific state */
	pkt = OUT_PKT(ring, G3D_REQUEST_REGISTER_WRITE);

	OUT_RING(ring, REG_FGRA_D_OFF_EN);
	OUT_RING(ring, 1);
	OUT_RING(ring, REG_FGRA_D_OFF_FACTOR);
	OUT_RING(ring, fui(depth));
	OUT_RING(ring, REG_FGRA_D_OFF_UNITS);
	OUT_RING(ring, fui(0.0f));
	OUT_RING(ring, REG_FGRA_BFCULL);
	OUT_RING(ring, 0);

	OUT_RING(ring, REG_FGPE_DEPTHRANGE_HALF_F_ADD_N);
	OUT_RING(ring, fui(0.0f));
	OUT_RING(ring, REG_FGPE_DEPTHRANGE_HALF_F_SUB_N);
	OUT_RING(ring, fui(1.0f));

	OUT_RING(ring, REG_FGPF_BLEND);
	OUT_RING(ring, 0);
	OUT_RING(ring, REG_FGPF_LOGOP);
	OUT_RING(ring, 0);

	if (!(buffers & PIPE_CLEAR_COLOR)) {
		OUT_RING(ring, REG_FGPF_CBMSK);
		OUT_RING(ring, FGPF_CBMSK_RED | FGPF_CBMSK_GREEN |
				FGPF_CBMSK_BLUE | FGPF_CBMSK_ALPHA);
	}

	OUT_RING(ring, REG_FGPF_ALPHAT);
	OUT_RING(ring, 0);

	if (buffers & PIPE_CLEAR_DEPTH) {
		OUT_RING(ring, REG_FGPF_DEPTHT);
		OUT_RING(ring, FGPF_DEPTHT_ENABLE |
				FGPF_DEPTHT_MODE(TEST_ALWAYS));
	} else {
		OUT_RING(ring, REG_FGPF_DEPTHT);
		OUT_RING(ring, 0);
	}

	if (buffers & PIPE_CLEAR_STENCIL) {
		OUT_RING(ring, REG_FGPF_FRONTST);
		OUT_RING(ring, FGPF_FRONTST_ENABLE |
				FGPF_FRONTST_MODE(TEST_ALWAYS) |
				FGPF_FRONTST_MASK(0xff) |
				FGPF_FRONTST_VALUE(stencil) |
				FGPF_FRONTST_SFAIL(STENCIL_KEEP) |
				FGPF_FRONTST_DPPASS(STENCIL_REPLACE) |
				FGPF_FRONTST_DPFAIL(STENCIL_KEEP));
		OUT_RING(ring, REG_FGPF_BACKST);
		OUT_RING(ring, FGPF_BACKST_MODE(TEST_NEVER) |
				FGPF_BACKST_MASK(0xff) |
				FGPF_BACKST_VALUE(stencil) |
				FGPF_BACKST_SFAIL(STENCIL_KEEP) |
				FGPF_BACKST_DPPASS(STENCIL_REPLACE) |
				FGPF_BACKST_DPFAIL(STENCIL_KEEP));
	} else {
		OUT_RING(ring, REG_FGPF_FRONTST);
		OUT_RING(ring, 0);
	}

	END_PKT(ring, pkt);

	/* emit draw */
	of_emit_draw(ctx, ctx->clear_vertex_info, OF_DIRTY_VTXSTATE |
			OF_DIRTY_VTXBUF | OF_DIRTY_RASTERIZER);

	ctx->dirty |= OF_DIRTY_ZSA |
			OF_DIRTY_VIEWPORT |
			OF_DIRTY_RASTERIZER |
			OF_DIRTY_SAMPLE_MASK |
			OF_DIRTY_PROG_VP |
			OF_DIRTY_PROG_FP |
			OF_DIRTY_CONSTBUF |
			OF_DIRTY_BLEND |
			OF_DIRTY_VTXSTATE |
			OF_DIRTY_VTXBUF;
	ctx->cso_active.rasterizer = NULL;
	ctx->cso_active.blend = NULL;
	ctx->cso_active.zsa = NULL;
	ctx->cso_active.vtx = NULL;
	ctx->cso_active.vp = NULL;
	ctx->cso_active.fp = NULL;

	if (of_mesa_debug & OF_DBG_DCLEAR)
		ctx->dirty = 0xffffffff;
}

static void
of_clear_render_target(struct pipe_context *pctx, struct pipe_surface *ps,
		const union pipe_color_union *color,
		unsigned x, unsigned y, unsigned w, unsigned h)
{
	DBG("TODO: x=%u, y=%u, w=%u, h=%u", x, y, w, h);
}

static void
of_clear_depth_stencil(struct pipe_context *pctx, struct pipe_surface *ps,
		unsigned buffers, double depth, unsigned stencil,
		unsigned x, unsigned y, unsigned w, unsigned h)
{
	DBG("TODO: buffers=%u, depth=%f, stencil=%u, x=%u, y=%u, w=%u, h=%u",
			buffers, depth, stencil, x, y, w, h);
}

static const float clear_vertices[] = {
	+1.0f, +1.0f, +1.0f, // RT
	-1.0f, +1.0f, +1.0f, // LT
	-1.0f, -1.0f, +1.0f, // LB

	+1.0f, +1.0f, +1.0f, // LT
	-1.0f, -1.0f, +1.0f, // RB
	+1.0f, -1.0f, +1.0f, // LB
};

static struct of_vertex_stateobj solid_vertex_stateobj = {
	.num_elements = 1,
	.elements = {
		[0] = {
			.attrib = 0x800072e4,
			.vbctrl = 0x0c00ffff,
			.vbbase = 0x00000000,
		},
	},
};

struct of_vertex_info *
of_draw_init_solid(struct of_context *ctx)
{
	struct of_vertex_info *info = CALLOC_STRUCT(of_vertex_info);
	struct of_draw_info *draw = &info->key;
	struct pipe_transfer *dst_transfer = NULL;
	struct of_vertex_buffer *buffer;
	float *dst;

	if (!info)
		return NULL;

	draw->base.vtx = &solid_vertex_stateobj;
	info->draw_mode = PTYPE_TRIANGLES;
	info->first_draw = false;
	info->bypass_cache = false;
	LIST_INITHEAD(&info->buffers);

	buffer = of_get_batch_buffer(ctx);
	if (!buffer) {
		FREE(info);
		return NULL;
	}

	dst = pipe_buffer_map(&ctx->base, buffer->buffer, PIPE_TRANSFER_WRITE,
				&dst_transfer);

	memcpy(dst, clear_vertices, sizeof(clear_vertices));

	if (dst_transfer)
		pipe_buffer_unmap(&ctx->base, dst_transfer);

	buffer->bytes_used = ROUND_UP(sizeof(clear_vertices), 32);
	buffer->nr_vertices = sizeof(clear_vertices) / (3 * sizeof(float));
	LIST_ADDTAIL(&buffer->list, &info->buffers);

	return info;
}

void
of_draw_init(struct pipe_context *pctx)
{
	struct of_context *ctx = of_context(pctx);

	pctx->draw_vbo = of_draw_vbo;
	pctx->clear = of_clear;
	pctx->clear_render_target = of_clear_render_target;
	pctx->clear_depth_stencil = of_clear_depth_stencil;

	ctx->draw_hash = cso_hash_create();
	ctx->draw = CALLOC_STRUCT(of_draw_info);
}

void
of_draw_fini(struct pipe_context *pctx)
{
	struct of_context *ctx = of_context(pctx);
	struct of_vertex_info *info = ctx->clear_vertex_info;

	if (ctx->draw)
		FREE(ctx->draw);

	if (info) {
		struct of_vertex_buffer *buf, *n;

		LIST_FOR_EACH_ENTRY_SAFE(buf, n, &info->buffers, list) {
			pipe_resource_reference(&buf->buffer, NULL);
			FREE(buf);
		}

		FREE(info);
	}
}
