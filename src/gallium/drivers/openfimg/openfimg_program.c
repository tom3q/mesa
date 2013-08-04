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
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"

#include "openfimg_program.h"
#include "openfimg_compiler.h"
#include "openfimg_texture.h"
#include "openfimg_util.h"

static struct of_shader_stateobj *
create_shader(enum shader_t type)
{
	struct of_shader_stateobj *so = CALLOC_STRUCT(of_shader_stateobj);
	if (!so)
		return NULL;
	so->type = type;
	return so;
}

static void
delete_shader(struct of_shader_stateobj *so)
{
	ir2_shader_destroy(so->ir);
	free(so->tokens);
	free(so->bin);
	free(so);
}

static struct of_shader_stateobj *
assemble(struct of_shader_stateobj *so)
{
	free(so->bin);
	so->bin = ir2_shader_assemble(so->ir, &so->info);
	if (!so->bin)
		goto fail;

	if (of_mesa_debug & OF_DBG_DISASM) {
		DBG("disassemble: type=%d", so->type);
		disasm_fimg_3dse(so->bin, so->info.sizedwords, 0, so->type);
	}

	return so;

fail:
	debug_error("assemble failed!");
	delete_shader(so);
	return NULL;
}

static struct of_shader_stateobj *
compile(struct of_program_stateobj *prog, struct of_shader_stateobj *so)
{
	int ret;

	if (of_mesa_debug & OF_DBG_DISASM) {
		DBG("dump tgsi: type=%d", so->type);
		tgsi_dump(so->tokens, 0);
	}

	ret = of_compile_shader(prog, so);
	if (ret)
		goto fail;

	/* NOTE: we don't assemble yet because for VS we don't know the
	 * type information for vertex fetch yet.. so those need to be
	 * patched up later before assembling.
	 */

	so->info.sizedwords = 0;

	return so;

fail:
	debug_error("compile failed!");
	delete_shader(so);
	return NULL;
}

static void
emit(struct of_ringbuffer *ring, struct of_shader_stateobj *so)
{
	unsigned i;

	if (so->info.sizedwords == 0)
		assemble(so);

#warning TODO
}

static void *
of_fp_state_create(struct pipe_context *pctx,
		const struct pipe_shader_state *cso)
{
	struct of_shader_stateobj *so = create_shader(SHADER_FRAGMENT);
	if (!so)
		return NULL;
	so->tokens = tgsi_dup_tokens(cso->tokens);
	return so;
}

static void
of_fp_state_delete(struct pipe_context *pctx, void *hwcso)
{
	struct of_shader_stateobj *so = hwcso;
	delete_shader(so);
}

static void
of_fp_state_bind(struct pipe_context *pctx, void *hwcso)
{
	struct of_context *ctx = of_context(pctx);
	ctx->prog.fp = hwcso;
	ctx->prog.dirty |= OF_SHADER_DIRTY_FP;
	ctx->dirty |= OF_DIRTY_PROG;
}

static void *
of_vp_state_create(struct pipe_context *pctx,
		const struct pipe_shader_state *cso)
{
	struct of_shader_stateobj *so = create_shader(SHADER_VERTEX);
	if (!so)
		return NULL;
	so->tokens = tgsi_dup_tokens(cso->tokens);
	return so;
}

static void
of_vp_state_delete(struct pipe_context *pctx, void *hwcso)
{
	struct of_shader_stateobj *so = hwcso;
	delete_shader(so);
}

static void
of_vp_state_bind(struct pipe_context *pctx, void *hwcso)
{
	struct of_context *ctx = of_context(pctx);
	ctx->prog.vp = hwcso;
	ctx->prog.dirty |= OF_SHADER_DIRTY_VP;
	ctx->dirty |= OF_DIRTY_PROG;
}

void
of_program_validate(struct of_context *ctx)
{
	struct of_program_stateobj *prog = &ctx->prog;

	/* if vertex or frag shader is dirty, we may need to recompile. Compile
	 * frag shader first, as that assigns the register slots for exports
	 * from the vertex shader.  And therefore if frag shader has changed we
	 * need to recompile both vert and frag shader.
	 */
	if (prog->dirty & OF_SHADER_DIRTY_FP)
		compile(prog, prog->fp);

	if (prog->dirty & (OF_SHADER_DIRTY_FP | OF_SHADER_DIRTY_VP))
		compile(prog, prog->vp);

	if (prog->dirty)
		ctx->dirty |= OF_DIRTY_PROG;
}

void
of_program_emit(struct of_ringbuffer *ring,
		struct of_program_stateobj *prog)
{
	struct ir2_shader_info *vsi =
		&((struct of_shader_stateobj *)prog->vp)->info;
	struct ir2_shader_info *fsi =
		&((struct of_shader_stateobj *)prog->fp)->info;
	uint8_t vs_gprs, fs_gprs, vs_export;

	emit(ring, prog->vp);
	emit(ring, prog->fp);

	vs_gprs = (vsi->max_reg < 0) ? 0x80 : vsi->max_reg;
	fs_gprs = (fsi->max_reg < 0) ? 0x80 : fsi->max_reg;
	vs_export = MAX2(1, prog->num_exports) - 1;

#warning TODO

	prog->dirty = 0;
}

void
of_prog_init(struct pipe_context *pctx)
{
	struct of_context *ctx = of_context(pctx);

	pctx->create_fs_state = of_fp_state_create;
	pctx->bind_fs_state = of_fp_state_bind;
	pctx->delete_fs_state = of_fp_state_delete;

	pctx->create_vs_state = of_vp_state_create;
	pctx->bind_vs_state = of_vp_state_bind;
	pctx->delete_vs_state = of_vp_state_delete;
}

void
of_prog_fini(struct pipe_context *pctx)
{

}
