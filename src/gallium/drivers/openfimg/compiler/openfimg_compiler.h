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

#ifndef OF_COMPILER_H_
#define OF_COMPILER_H_

#include "openfimg_program.h"
#include "openfimg_util.h"

/**
 * Compiles specified shader program into driver's internal representation.
 * @param so Shader state object initialized with valid TGSI program.
 * @return Zero on success, non-zero on failure.
 */
int of_shader_compile(struct of_shader_stateobj *so);

/**
 * Assembles specified shader program into binary code.
 * @param ctx Driver's pipe context for which the program should be assembled.
 * @param so Shader state object that has been successfully compiled.
 * @return Zero on success, non-zero on failure.
 */
int of_shader_assemble(struct of_context *ctx, struct of_shader_stateobj *so);

/**
 * Destroys specified shader program.
 * @param so Shader state object.
 */
void of_shader_destroy(struct of_shader_stateobj *so);

/**
 * Disassembles existing binary code and prints the output to debugging output.
 * @param ctx Driver's pipe context for buffer mapping purposes.
 * @param buffer Pipe resource containing binary code to disassemble.
 * @param sizedwords Size of binary code in 32-bit words.
 * @param type Target shader unit of specified binary code.
 * @return Zero on success, non-zero on failure.
 */
int of_shader_disassemble(struct of_context *ctx, struct pipe_resource *buffer,
			     unsigned sizedwords, enum of_shader_type type);

#endif /* OF_COMPILER_H_ */
