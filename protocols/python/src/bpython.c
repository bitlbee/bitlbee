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

#include <Python.h>
#include "bytesobject.h"
#include "structmember.h"

#include <bitlbee.h>

/* Python module classes: */

typedef struct {
    PyObject_HEAD
    PyObject * name; /* protocol name */
} Protocol;

static void Protocol_dealloc(Protocol * self) {
    Py_XDECREF(self->name);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject * Protocol_new(PyTypeObject * type, PyObject * args, PyObject * kwds) {
    Protocol * self;

    self = (Protocol *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->name = PyString_FromString("unnamed");
        if (self->name == NULL) {
            Py_DECREF(self);
            return NULL;
        }
    }

    return (PyObject *)self;
}

static int Protocol_init(Protocol *self, PyObject *args, PyObject *kwds)
{
    PyObject * name;
    PyObject * tmp;

    static char *kwlist[] = {"name", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, 
                                      &name))
        return -1; 

    if (name) {
        tmp = self->name;
        Py_INCREF(name);
        self->name = name;
        Py_XDECREF(tmp);
    }

    return 0;
}

static PyMemberDef Protocol_members[] = {
    {"name", T_OBJECT_EX, offsetof(Protocol, name), 0,
     "protocol name (used in 'account add')"},
    {NULL}  /* Sentinel */
};


static PyMethodDef Protocol_methods[] = {
    //{"name", (PyCFunction)Noddy_name, METH_NOARGS,
    // "Return the name, combining the first and last name"
    //},
    {NULL}  /* Sentinel */
};


static PyTypeObject ProtocolType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "bpython.Protocol",  /*tp_name*/
    sizeof(Protocol), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Protocol_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    "Protocol plugin objects",           /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    Protocol_methods,             /* tp_methods */
    Protocol_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Protocol_init,      /* tp_init */
    0,                         /* tp_alloc */
    Protocol_new,                 /* tp_new */
};

/* Python module functions: */

static PyObject * bpython_register_protocol(PyObject *self, PyObject *args)
{
    PyObject *protocol;
    char * name;
    struct prpl * ret;

    if (!PyArg_ParseTuple(args, "sO", &name, &protocol))
        return NULL;
    
    /* Increase the reference count to stop garbage collection */
    Py_INCREF(protocol);

    ret = g_new0(struct prpl, 1);
    ret->name = name;
    ret->data = ret;

    register_protocol(ret);

    Py_RETURN_NONE;
}

static PyMethodDef bpython_methods[] = {
    {"register_protocol",  bpython_register_protocol, METH_VARARGS,
     "Register a protocol."},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

PyMODINIT_FUNC initbpython(void)
{
    PyObject * m;

    ProtocolType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&ProtocolType) < 0) {
        return;
    }

    m = Py_InitModule3("bpython", bpython_methods, "Bitlbee Plugin module");

    Py_INCREF(&ProtocolType);
    PyModule_AddObject(m, "Protocol", (PyObject *)&ProtocolType);
}


/* Bitlbee plugin functions: */

void init_plugin() {
    GDir *dir;
    GError *error = NULL;

    printf("Initialising Python... ");
    Py_Initialize();
    PyRun_SimpleString("print 'Python initialised!'\n");
    
    /* Add our static module */
    initbpython();

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
                FILE * pyfile;
                PyObject * result;
		printf("Loading python file %s\n", path);
                pyfile = fopen(path, "r");
                
                /* Copy main dict to make sure that separate plugins
                   run in separate environments */
                PyObject * main_dict_copy = PyDict_Copy(main_dict);

                /* Run the python file */
                result = PyRun_File(pyfile, path, Py_file_input, main_dict_copy, main_dict_copy);
		if (result == NULL) {
		    printf("Error loading module %s\n", path);
		    PyErr_PrintEx(0);
		}
            }

            g_free(path);
        }
    }

    // Py_Finalize(); // TODO - there's no plugin_unload for us to do this in.
}

int main(int argc, char ** argv) {
    printf("This is a bitlbee plugin, you should place it in bitlbee's plugins directory.");
    return 0;
}
