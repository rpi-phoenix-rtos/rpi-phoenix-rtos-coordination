#define PY_SSIZE_T_CLEAN
#include <Python.h>

static PyObject *spam_add(PyObject *self, PyObject *args)
{
	long a, b;
	if (!PyArg_ParseTuple(args, "ll", &a, &b)) {
		return NULL;
	}
	return PyLong_FromLong(a + b);
}

static PyMethodDef SpamMethods[] = {
	{ "add", spam_add, METH_VARARGS, "add two ints" },
	{ NULL, NULL, 0, NULL }
};

static struct PyModuleDef spammodule = {
	PyModuleDef_HEAD_INIT, "spam", "Phoenix dlopen demo extension", -1, SpamMethods
};

PyMODINIT_FUNC PyInit_spam(void)
{
	return PyModule_Create(&spammodule);
}
