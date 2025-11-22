#define HAVE_SNPRINTF
#undef _DEBUG
#include "Python.h"
#define _DEBUG

#include "pyLib.h"
#include "pyLibContainer.h"
#include "sharedLibContainer.h"

static PyObject* pyXMembersIterator(const char *xpath);
static PyObject* pyXIndicesIterator(const char *xpath);

static PyObject *spFuncBegin = NULL;
static PyObject *spFuncProcess = NULL;
static PyObject *spFuncEnd = NULL;

static Container *spContainer = NULL;
static U32 suModTime = 0;

// --------------------------------------------------------------------------

static PyObject *dbscript_xvalue(PyObject *self, PyObject *args)
{
	static char *result = NULL;
    const char *xpath;

	PERFINFO_AUTO_START_FUNC();

    if (!PyArg_ParseTuple(args, "s", &xpath))
	{
		PyErr_BadArgument();
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	estrCopy2(&result, "");
	if(!objPathGetEString(xpath, spContainer->containerSchema->classParse, spContainer->containerData, &result))
	{
		estrCopy2(&result, "");
	}

	PERFINFO_AUTO_STOP();
    return Py_BuildValue("s", result);
}

static PyObject *dbscript_xcount(PyObject *self, PyObject *args)
{
    const char *xpath;
	int count = 0;

	PERFINFO_AUTO_START_FUNC();
    if (PyArg_ParseTuple(args, "s", &xpath))
	{
		count = xcount(xpath, spContainer);
	}

	PERFINFO_AUTO_STOP();
    return Py_BuildValue("i", count);
}

static PyObject *dbscript_xtype(PyObject *self, PyObject *args)
{
	const char *xpath;

	PERFINFO_AUTO_START_FUNC();
	if (PyArg_ParseTuple(args, "s", &xpath))
	{
		VarType varType = xtype(xpath, spContainer, NULL);
		if(varType != VARTYPE_UNKNOWN)
		{
			int iVarType = (int)varType;
			PERFINFO_AUTO_STOP();
			return Py_BuildValue("i", iVarType);
		}
	}

	PyErr_BadArgument();
	PERFINFO_AUTO_STOP();
	return NULL;
}

static PyObject *dbscript_xmembers(PyObject *self, PyObject *args)
{
	const char *xpath;

	PERFINFO_AUTO_START_FUNC();
	if (PyArg_ParseTuple(args, "s", &xpath))
	{
		PERFINFO_AUTO_STOP();
		return pyXMembersIterator(xpath);
	}

	PyErr_BadArgument();
	PERFINFO_AUTO_STOP();
	return NULL;
}

static PyObject *dbscript_xindices(PyObject *self, PyObject *args)
{
	const char *xpath;

	PERFINFO_AUTO_START_FUNC();
	if (PyArg_ParseTuple(args, "s", &xpath))
	{
		PERFINFO_AUTO_STOP();
		return pyXIndicesIterator(xpath);
	}

	PyErr_BadArgument();
	PERFINFO_AUTO_STOP();
	return NULL;
}

static PyMethodDef DBScriptMethods[] = {
    {"xvalue",  dbscript_xvalue, METH_VARARGS, "Query an xpath's string value for the currently loaded container."},
    {"xcount",  dbscript_xcount, METH_VARARGS, "Query an array's xpath's count for the currently loaded container."},
	{"xtype",  dbscript_xtype, METH_VARARGS, "Query an xpath's type for the currently loaded container. Returns: NORMAL, ARRAY, STRUCT, or None"},
	{"xmembers",  dbscript_xmembers, METH_VARARGS, "Creates an iterator used to walk all member names of an xpath"},
	{"xindices",  dbscript_xindices, METH_VARARGS, "Creates an iterator used to walk all indices of an array's xpath"},
    {NULL, NULL, 0, NULL}
};

void pyInitDBScriptModule(const char *pScriptFilename, const char *pSnapshotFilename)
{
	PyObject *pDBScriptModule = Py_InitModule("dbscript", DBScriptMethods);

	PyModule_AddIntConstant(pDBScriptModule, "NORMAL", VARTYPE_NORMAL);
	PyModule_AddIntConstant(pDBScriptModule, "ARRAY",  VARTYPE_ARRAY);
	PyModule_AddIntConstant(pDBScriptModule, "STRUCT", VARTYPE_STRUCT);
	PyModule_AddStringConstant(pDBScriptModule, "SCRIPT", pScriptFilename);
	PyModule_AddStringConstant(pDBScriptModule, "SNAPSHOT", pSnapshotFilename);
}	

// ---------------------------------------------------------------------------
// XMembers

typedef struct {
	PyObject_HEAD
	XPathLookup l;
	int i;
} XMembersObject;

static PyObject *
xmembers_next(XMembersObject *r)
{
	PERFINFO_AUTO_START_FUNC();

	if(!r->l.tpi)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	while(r->l.tpi[r->i].type || (r->l.tpi[r->i].name && r->l.tpi[r->i].name[0]))
	{
		int type = TOK_GET_TYPE(r->l.tpi[r->i].type);
		if (type == TOK_START)   { r->i++; continue; }
		if (type == TOK_END)     { r->i++; continue; }
		if (type == TOK_IGNORE)  { r->i++; continue; }
		if (type == TOK_COMMAND) { r->i++; continue; }

		r->i++;
		PERFINFO_AUTO_STOP();
		return Py_BuildValue("s", r->l.tpi[r->i-1].name);
	}

	PERFINFO_AUTO_STOP();
	return NULL;
}

#pragma warning(push)
#pragma warning(disable:4232) // nonstandard extension used : 'x' : address of dllimport 'y' is not static, identity not guaranteed
static PyTypeObject PyXMembers_Type = {
	PyObject_HEAD_INIT(&PyXMembers_Type)
	0,                                      /* ob_size */
	"xmembersiterator",                        /* tp_name */
	sizeof(XMembersObject),                /* tp_basicsize */
	0,                                      /* tp_itemsize */
	/* methods */
	(destructor)PyObject_Del,		/* tp_dealloc */
	0,                                      /* tp_print */
	0,                                      /* tp_getattr */
	0,                                      /* tp_setattr */
	0,                                      /* tp_compare */
	0,                                      /* tp_repr */
	0,                                      /* tp_as_number */
	0,					/* tp_as_sequence */
	0,                                      /* tp_as_mapping */
	0,                                      /* tp_hash */
	0,                                      /* tp_call */
	0,                                      /* tp_str */
	PyObject_GenericGetAttr,                /* tp_getattro */
	0,                                      /* tp_setattro */
	0,                                      /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,			/* tp_flags */
	0,                                      /* tp_doc */
	0,					/* tp_traverse */
	0,                                      /* tp_clear */
	0,                                      /* tp_richcompare */
	0,                                      /* tp_weaklistoffset */
	PyObject_SelfIter,			/* tp_iter */
	(iternextfunc)xmembers_next,		/* tp_iternext */
	0,                          			/* tp_methods */
	0,
};
#pragma warning(pop)

static PyObject* pyXMembersIterator(const char *xpath)
{
	XMembersObject *it = PyObject_New(XMembersObject, &PyXMembers_Type);
	PERFINFO_AUTO_START_FUNC();
	if (it == NULL)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	memset(&it->l, 0, sizeof(it->l));
	xlookup(xpath, spContainer, &it->l, true);
	it->i = 0;
	PERFINFO_AUTO_STOP();
	return (PyObject *)it;
}

// ---------------------------------------------------------------------------
// XIndices

typedef struct {
	PyObject_HEAD
	XPathLookup l;
	int numelems;
	int indexed;
	int i;
} XIndicesObject;

static PyObject *
xindices_next(XIndicesObject *r)
{
	int keyfield;
	ParseTable* subtable = NULL;
	char buf[MAX_TOKEN_LENGTH];

	PERFINFO_AUTO_START_FUNC();

	if(!r->l.tpi)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	if(r->indexed)
	{
		if(r->i < r->numelems)
		{
			void* substruct = StructGetSubtable(r->l.tpi, r->l.column, r->l.ptr, r->i, &subtable, NULL);
			if (!substruct)
			{
				PERFINFO_AUTO_STOP();
				return NULL;
			}
			keyfield = ParserGetTableKeyColumn(subtable);
			assertmsg(keyfield >= 0, "Some polymorph types of have a key field, but some do not?? BAD");
			if (TokenToSimpleString(subtable, keyfield, substruct, SAFESTR(buf), false))
			{
				r->i++;
				PERFINFO_AUTO_STOP();
				return Py_BuildValue("s", buf);
			}
		}
	}
	else
	{
		if(r->i < r->numelems)
		{
			r->i++;
			PERFINFO_AUTO_STOP();
			return Py_BuildValue("i", r->i-1);
		}
	}

	PERFINFO_AUTO_STOP();
	return NULL;
}

static void
xindices_free(XIndicesObject *self)
{
	PyObject_Del(self);
}

#pragma warning(push)
#pragma warning(disable:4232) // nonstandard extension used : 'x' : address of dllimport 'y' is not static, identity not guaranteed
static PyTypeObject PyXIndices_Type = {
	PyObject_HEAD_INIT(&PyXIndices_Type)
	0,                                      /* ob_size */
	"xindicesiterator",                        /* tp_name */
	sizeof(XIndicesObject),                /* tp_basicsize */
	0,                                      /* tp_itemsize */
	/* methods */
	(destructor)xindices_free,		/* tp_dealloc */
	0,                                      /* tp_print */
	0,                                      /* tp_getattr */
	0,                                      /* tp_setattr */
	0,                                      /* tp_compare */
	0,                                      /* tp_repr */
	0,                                      /* tp_as_number */
	0,					/* tp_as_sequence */
	0,                                      /* tp_as_mapping */
	0,                                      /* tp_hash */
	0,                                      /* tp_call */
	0,                                      /* tp_str */
	PyObject_GenericGetAttr,                /* tp_getattro */
	0,                                      /* tp_setattro */
	0,                                      /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,			/* tp_flags */
	0,                                      /* tp_doc */
	0,					/* tp_traverse */
	0,                                      /* tp_clear */
	0,                                      /* tp_richcompare */
	0,                                      /* tp_weaklistoffset */
	PyObject_SelfIter,			/* tp_iter */
	(iternextfunc)xindices_next,		/* tp_iternext */
	0,                          			/* tp_methods */
	0,
};
#pragma warning(pop)

static PyObject* pyXIndicesIterator(const char *xpath)
{
	XIndicesObject *it = PyObject_New(XIndicesObject, &PyXIndices_Type);
	if (it == NULL)
		return NULL;

	PERFINFO_AUTO_START_FUNC();
	memset(&it->l, 0, sizeof(it->l));
	it->numelems = 0;
	it->indexed = 0;
	it->i = 0;

	if(xlookup(xpath, spContainer, &it->l, false))
	{
		it->numelems = TokenStoreGetNumElems(it->l.tpi, it->l.column, it->l.ptr, NULL);
		it->indexed = ParserColumnIsIndexedEArray(it->l.tpi, it->l.column, NULL);
		it->i = 0;
	}
	PERFINFO_AUTO_STOP();

	return (PyObject *)it;
}

// ---------------------------------------------------------------------------

bool pyLibContainerInit(PyObject *pMainModule)
{
	bool ret = true;
	spFuncBegin   = pyLibGetFuncSafe(pMainModule, "Begin");
	spFuncProcess = pyLibGetFuncSafe(pMainModule, "Process");
	spFuncEnd     = pyLibGetFuncSafe(pMainModule, "End");

	if(!spFuncBegin)
	{
		fprintf(fileGetStderr(), "Warning: Begin() function absent.\n");
	}
	if(!spFuncProcess)
	{
		fprintf(fileGetStderr(), "ERROR: Process() function absent.\n");
		ret = false;
	}
	if(!spFuncEnd)
	{
		fprintf(fileGetStderr(), "Warning: End() function absent.\n");
	}

	return ret;
}

void pyLibContainerShutdown()
{
	Py_XDECREF(spFuncBegin);
	spFuncBegin = NULL;

	Py_XDECREF(spFuncProcess);
	spFuncProcess = NULL;

	Py_XDECREF(spFuncEnd);
	spFuncEnd = NULL;
}

void pyBegin()
{
	PERFINFO_AUTO_START_FUNC();
	if(spFuncBegin)
	{
		PyObject_CallObject(spFuncBegin, NULL);
		PyErr_Print();
	}
	PERFINFO_AUTO_STOP();
}

void pyEnd()
{
	PERFINFO_AUTO_START_FUNC();
	if(spFuncEnd)
	{
		PyObject_CallObject(spFuncEnd, NULL);
		PyErr_Print();
	}
	PERFINFO_AUTO_STOP();
}

bool pyProcessContainer(Container *con, U32 uContainerModifiedTime)
{
	PERFINFO_AUTO_START_FUNC();
	if(spFuncProcess)
	{
		spContainer = con;
		suModTime = uContainerModifiedTime;

		PyObject_CallObject(spFuncProcess, NULL);
		PyErr_Print();

		spContainer = NULL;
		suModTime = 0;
	}
	PERFINFO_AUTO_STOP();
	return true;
}
