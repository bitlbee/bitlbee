/*
 * bpython.c - Python plugin for bitlbee
 * 
 * Copyright (c) 2013 Nick Murdoch
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <bitlbee.h>
#include <Python.h>

void init_plugin() {
    struct prpl *ret = g_new0(struct prpl, 1);

    printf("Initialising Python... ");
    Py_Initialize();
    PyRun_SimpleString("print 'Python initialised!'\n");
    // Py_Finalize(); // TODO - there's no plugin_unload for us to do this in.

    ret->name = "bpython";

    register_protocol(ret);
}

int main(int argc, char ** argv) {
    printf("This is a bitlbee plugin, you should place it in bitlbee's plugins directory.");
    return 0;
}
