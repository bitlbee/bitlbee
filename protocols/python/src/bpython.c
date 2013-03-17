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
    GDir *dir;
    GError *error = NULL;

    printf("Initialising Python... ");
    Py_Initialize();
    PyRun_SimpleString("print 'Python initialised!'\n");
    
    /* Get a reference to the main module. */
    PyObject* main_module = PyImport_AddModule("__main__");

    /* Get the main module's dictionary */
    PyObject* main_dict = PyModule_GetDict(main_module);

    dir = g_dir_open(global.conf->plugindir, 0, &error);
    if (dir) {
        const gchar *entry;
        char * path;
        while ((entry = g_dir_read_name(dir))) {
            path = g_build_filename(global.conf->plugindir, entry, NULL);
            if (!path) {
                log_message(LOGLVL_WARNING, "Can't build path for %s\n", entry);
                continue;
            }
            
            if (g_str_has_suffix(path, ".py")) {
                printf("Loading python file %s\n", path);
                FILE* pyfile = fopen(path, "r");
                /* Copy main dict to make sure that separate plugins
                   run in separate environments */
                PyObject* main_dict_copy = PyDict_Copy(main_dict);
                PyRun_File(pyfile, path, Py_file_input, main_dict_copy, main_dict_copy);
                
            }

            g_free(path);
        }
    }


    ret->name = "bpython";

    register_protocol(ret);

    // Py_Finalize(); // TODO - there's no plugin_unload for us to do this in.
}

int main(int argc, char ** argv) {
    printf("This is a bitlbee plugin, you should place it in bitlbee's plugins directory.");
    return 0;
}
