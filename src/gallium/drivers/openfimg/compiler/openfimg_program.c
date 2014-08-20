/*
 * Copyright (C) 2013-2014 Tomasz Figa <tomasz.figa@gmail.com>
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
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_text.h"

#include "openfimg_program.h"
#include "openfimg_compiler.h"
#include "openfimg_texture.h"
#include "openfimg_resource.h"
#include "openfimg_util.h"
#include "openfimg_state.h"
#include "openfimg_vertex.h"

/*
 * Shader overriding support
 */
static int
override_shader(struct of_context *ctx, struct of_shader_stateobj *so)
{
	char path[] = "Xs_YYYYYYYY.bin";
	struct pipe_transfer *transfer = NULL;
	struct pipe_resource *buffer = NULL;
	struct shader_header hdr;
	uint32_t *dwords = NULL;
	FILE *file;
	size_t ret;

	snprintf(path, sizeof(path), "%s_%08x.bin",
			(so->type == OF_SHADER_VERTEX) ? "vs" : "fs",
			so->hash);

	DBG("%s: looking for replacement shader in '%s'", __func__, path);

	file = fopen(path, "rb");
	if (!file)
		return -1;

	DBG("%s: loading shader from '%s'", __func__, path);

	ret = fread(&hdr, sizeof(hdr), 1, file);
	if (ret != 1) {
		DBG("truncated shader binary file");
		goto fail;
	}

	if (fseek(file, hdr.header_size, SEEK_SET)) {
		DBG("truncated shader binary file");
		goto fail;
	}

	buffer = pipe_buffer_create(ctx->base.screen,
					PIPE_BIND_CUSTOM, PIPE_USAGE_IMMUTABLE,
					16 * hdr.instruct_size);
	if (!buffer) {
		DBG("shader BO allocation failed");
		goto fail;
	}

	dwords = pipe_buffer_map(&ctx->base, buffer, PIPE_TRANSFER_WRITE,
					&transfer);
	if (!dwords) {
		DBG("failed to map shader BO");
		goto fail;
	}

	ret = fread(dwords, 16, hdr.instruct_size, file);
	if (ret != hdr.instruct_size) {
		DBG("truncated shader binary file");
		goto fail;
	}

	if (hdr.const_float_size > so->first_immediate) {
		uint32_t *immediates;

		hdr.const_float_size -= so->first_immediate;

		immediates = CALLOC(hdr.const_float_size, 16);
		assert(immediates);

		if (fseek(file, 16 * so->first_immediate, SEEK_CUR)) {
			DBG("truncated shader binary file");
			goto fail;
		}

		ret = fread(immediates, 16, hdr.const_float_size, file);
		if (ret != hdr.const_float_size) {
			DBG("truncated shader binary file");
			goto fail;
		}

		FREE(so->immediates);
		so->immediates = immediates;
		so->num_immediates = 4 * hdr.const_float_size;
	}

	so->buffer = buffer;
	so->num_instrs = hdr.instruct_size;

	if (transfer)
		pipe_buffer_unmap(&ctx->base, transfer);

	fclose(file);

	DBG("%s: successfully loaded shader '%s'", __func__, path);

	return 0;

fail:
	fclose(file);
	if (transfer)
		pipe_buffer_unmap(&ctx->base, transfer);
	if (buffer)
		pipe_resource_reference(&buffer, NULL);

	return -1;
}

/*
 * Compilation wrappers
 */
static int
compile(struct of_shader_stateobj *so)
{
	int ret;

	if (of_mesa_debug & OF_DBG_DISASM) {
		DBG("dump tgsi: type=%d", so->type);
		tgsi_dump(so->tokens, 0);
	}

	ret = of_compile_shader(so);
	if (ret) {
		debug_error("compile failed!");
		return -1;
	}

	return 0;
}

static int
assemble(struct of_context *ctx, struct of_shader_stateobj *so)
{
	int ret;

	if (!so->ir) {
		ret = compile(so);
		if (ret)
			return -1;
	}

	if (of_mesa_debug & OF_DBG_SHADER_OVERRIDE) {
		ret = override_shader(ctx, so);
		if (!ret)
			goto overridden;
	}

	ret = of_ir_shader_assemble(ctx, so->ir, so);
	if (ret) {
		debug_error("assemble failed!");
		return -1;
	}

	if (so->num_instrs > 512) {
		debug_error("assembled shader too big (%u > 512)");
		return -1;
	}

overridden:
	if (of_mesa_debug & OF_DBG_DISASM) {
		DBG("disassemble: type=%d", so->type);
		of_ir_shader_disassemble(ctx, so->buffer, 4 * so->num_instrs,
						so->type);
	}

	return 0;
}

void
of_program_emit(struct of_context *ctx, struct of_shader_stateobj *so)
{
	struct fd_ringbuffer *ring = ctx->ring;
	uint32_t *pkt;
	int ret;

	if (!so->buffer) {
		ret = assemble(ctx, so);
		if (ret) {
			DBG("failed to assemble shader, using dummy!");
			so->num_instrs = 0;
			pipe_resource_reference(&so->buffer, ctx->dummy_shader);
			return;
		}
	}

	pkt = OUT_PKT(ring, G3D_REQUEST_SHADER_PROGRAM);
	OUT_RING(ring, so->type << 8);
	OUT_RING(ring, 4 * (so->first_immediate + so->num_immediates));
	OUT_RING(ring, 0);
	OUT_RING(ring, fd_bo_handle(of_resource(so->buffer)->bo));
	OUT_RING(ring, 0);
	OUT_RING(ring, so->num_instrs * 16);
	END_PKT(ring, pkt);

	if (so->type == OF_SHADER_VERTEX) {
		unsigned i;

		pkt = OUT_PKT(ring, G3D_REQUEST_REGISTER_WRITE);

		OUT_RING(ring, REG_FGVS_ATTRIBUTE_NUM);
		OUT_RING(ring, ctx->cso.vtx->num_elements);
		for (i = 0; i < 3; ++i) {
			OUT_RING(ring, REG_FGVS_IN_ATTR_INDEX(i));
			OUT_RING(ring, so->input_map[i]);
			OUT_RING(ring, REG_FGVS_OUT_ATTR_INDEX(i));
			OUT_RING(ring, so->output_map[i]);
		}

		END_PKT(ring, pkt);
	}
}

/*
 * State management
 */
static struct of_shader_stateobj *
create_shader(struct of_context *ctx, const struct pipe_shader_state *cso,
	      enum of_shader_type type)
{
	struct of_shader_stateobj *so;
	unsigned n;

	so = CALLOC_STRUCT(of_shader_stateobj);
	if (!so)
		return NULL;

	n = tgsi_num_tokens(cso->tokens);
	so->tokens = tgsi_dup_tokens(cso->tokens);
	so->hash = of_hash_oneshot(cso->tokens, n * sizeof(struct tgsi_token));
	so->type = type;

	so->input_map[0] = 0x03020100;
	so->input_map[1] = 0x07060504;
	so->input_map[2] = 0x0b0a0908;

	so->output_map[0] = 0x03020100;
	so->output_map[1] = 0x07060504;
	so->output_map[2] = 0x0b0a0908;

	return so;
}

static void *
of_fp_state_create(struct pipe_context *pctx,
		const struct pipe_shader_state *cso)
{
	struct of_context *ctx = of_context(pctx);
	return create_shader(ctx, cso, OF_SHADER_PIXEL);
}

static void
of_fp_state_bind(struct pipe_context *pctx, void *hwcso)
{
	OF_CSO_BIND(pctx, fp, OF_DIRTY_PROG_FP, hwcso);
}

static void *
of_vp_state_create(struct pipe_context *pctx,
		const struct pipe_shader_state *cso)
{
	struct of_context *ctx = of_context(pctx);
	return create_shader(ctx, cso, OF_SHADER_VERTEX);
}

static void
of_vp_state_bind(struct pipe_context *pctx, void *hwcso)
{
	OF_CSO_BIND(pctx, vp, OF_DIRTY_PROG_VP, hwcso);
}

static void
of_prog_state_delete(struct pipe_context *pctx, void *hwcso)
{
	struct of_shader_stateobj *so = hwcso;

	of_ir_shader_destroy(so->ir);
	free(so->tokens);
	pipe_resource_reference(&so->buffer, NULL);
	free(so);
}

/*
 * Utility programs
 */
static void *
assemble_tgsi(struct pipe_context *pctx, const char *src, bool frag)
{
	struct tgsi_token toks[32];
	struct pipe_shader_state cso = {
		.tokens = toks,
	};

	tgsi_text_translate(src, toks, ARRAY_SIZE(toks));

	if (frag)
		return pctx->create_fs_state(pctx, &cso);
	else
		return pctx->create_vs_state(pctx, &cso);
}

static const char *solid_fp =
	"FRAG                                        \n"
	"PROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1       \n"
	"DCL CONST[0]                                \n"
	"DCL OUT[0], COLOR                           \n"
	"  0: MOV OUT[0], CONST[0]                   \n"
	"  1: END                                    \n";

static const char *solid_vp =
	"VERT                                        \n"
	"DCL IN[0]                                   \n"
	"DCL OUT[0], POSITION                        \n"
	"DCL CONST[0]                                \n"
	"  0: MOV OUT[0].z, CONST[0].xxxx            \n"
	"  1: MOV OUT[0].xyw, IN[0]                  \n"
	"  2: END                                    \n";

void
of_prog_init_solid(struct of_context *ctx)
{
	ctx->solid_fp = assemble_tgsi(&ctx->base, solid_fp, true);
	ctx->solid_vp = assemble_tgsi(&ctx->base, solid_vp, false);
}

static const char *blit_fp =
	"FRAG                                        \n"
	"PROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1       \n"
	"DCL IN[0], TEXCOORD                         \n"
	"DCL OUT[0], COLOR                           \n"
	"DCL SAMP[0]                                 \n"
	"  0: TEX OUT[0], IN[0], SAMP[0], 2D         \n"
	"  1: END                                    \n";

static const char *blit_vp =
	"VERT                                        \n"
	"DCL IN[0]                                   \n"
	"DCL IN[1]                                   \n"
	"DCL OUT[0], TEXCOORD                        \n"
	"DCL OUT[1], POSITION                        \n"
	"  0: MOV OUT[0], IN[0]                      \n"
	"  0: MOV OUT[1], IN[1]                      \n"
	"  1: END                                    \n";

void
of_prog_init_blit(struct of_context *ctx)
{
	ctx->blit_fp = assemble_tgsi(&ctx->base, blit_fp, true);
	ctx->blit_vp = assemble_tgsi(&ctx->base, blit_vp, false);
}

/*
 * Context init/fini
 */
void
of_prog_init(struct pipe_context *pctx)
{
	struct of_context *ctx = of_context(pctx);

	pctx->create_fs_state = of_fp_state_create;
	pctx->bind_fs_state = of_fp_state_bind;
	pctx->delete_fs_state = of_prog_state_delete;

	pctx->create_vs_state = of_vp_state_create;
	pctx->bind_vs_state = of_vp_state_bind;
	pctx->delete_vs_state = of_prog_state_delete;

	ctx->dummy_shader = pipe_buffer_create(ctx->base.screen,
						PIPE_BIND_CUSTOM,
						PIPE_USAGE_IMMUTABLE, 4096);
	if (!ctx->dummy_shader)
		DBG("shader BO allocation failed");
}

void
of_prog_fini(struct pipe_context *pctx)
{
	struct of_context *ctx = of_context(pctx);

	if (ctx->solid_vp)
		pctx->delete_vs_state(pctx, ctx->solid_vp);
	if (ctx->solid_fp)
		pctx->delete_fs_state(pctx, ctx->solid_fp);

	if (ctx->blit_vp)
		pctx->delete_vs_state(pctx, ctx->blit_vp);
	if (ctx->blit_fp)
		pctx->delete_fs_state(pctx, ctx->blit_fp);

	pipe_resource_reference(&ctx->dummy_shader, NULL);
}
