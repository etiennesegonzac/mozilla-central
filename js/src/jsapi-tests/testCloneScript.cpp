/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99:
 *
 * Test script cloning.
 */

#include "tests.h"
#include "jsapi.h"
#include "jsdbgapi.h"
#include "jsxdrapi.h"

BEGIN_TEST(test_cloneScript)
{
    JSObject *A, *B;

    CHECK(A = createGlobal());
    CHECK(B = createGlobal());

    const char *source = 
        "var i = 0;\n"
        "var sum = 0;\n"
        "while (i < 10) {\n"
        "    sum += i;\n"
        "    ++i;\n"
        "}\n"
        "(sum);\n";

    JSObject *obj;

    // compile for A
    {
        JSAutoEnterCompartment a;
        if (!a.enter(cx, A))
            return false;

        JSFunction *fun;
        CHECK(fun = JS_CompileFunction(cx, A, "f", 0, NULL, source, strlen(source), __FILE__, 1));
        CHECK(obj = JS_GetFunctionObject(fun));
    }

    // clone into B
    {
        JSAutoEnterCompartment b;
        if (!b.enter(cx, B))
            return false;

        CHECK(JS_CloneFunctionObject(cx, obj, B));
    }

    return true;
}
END_TEST(test_cloneScript)

void
DestroyPrincipals(JSContext *cx, JSPrincipals *principals)
{
    delete principals;
}

struct Principals : public JSPrincipals
{
  public:
    Principals(const char *name)
    {
        refcount = 0;
        codebase = const_cast<char *>(name);
        destroy = DestroyPrincipals;
        subsume = NULL;
    }
};

class AutoDropPrincipals
{
    JSContext *cx;
    JSPrincipals *principals;

  public:
    AutoDropPrincipals(JSContext *cx, JSPrincipals *principals)
      : cx(cx), principals(principals)
    {
        JSPRINCIPALS_HOLD(cx, principals);
    }

    ~AutoDropPrincipals()
    {
        JSPRINCIPALS_DROP(cx, principals);
    }
};

JSBool
TranscodePrincipals(JSXDRState *xdr, JSPrincipals **principalsp)
{
    return JS_XDRBytes(xdr, reinterpret_cast<char *>(principalsp), sizeof(*principalsp));
}

BEGIN_TEST(test_cloneScriptWithPrincipals)
{
    JSSecurityCallbacks cbs = {
        NULL,
        TranscodePrincipals,
        NULL,
        NULL
    };

    JS_SetRuntimeSecurityCallbacks(rt, &cbs);

    JSPrincipals *principalsA = new Principals("A");
    AutoDropPrincipals dropA(cx, principalsA);
    JSPrincipals *principalsB = new Principals("B");
    AutoDropPrincipals dropB(cx, principalsB);

    JSObject *A, *B;

    CHECK(A = createGlobal(principalsA));
    CHECK(B = createGlobal(principalsB));

    const char *argnames[] = { "arg" };
    const char *source = "return function() { return arg; }";

    JSObject *obj;

    // Compile in A
    {
        JSAutoEnterCompartment a;
        if (!a.enter(cx, A))
            return false;

        JSFunction *fun;
        CHECK(fun = JS_CompileFunctionForPrincipals(cx, A, principalsA, "f",
                                                    mozilla::ArrayLength(argnames), argnames,
                                                    source, strlen(source), __FILE__, 1));

        JSScript *script;
        CHECK(script = JS_GetFunctionScript(cx, fun));

        CHECK(JS_GetScriptPrincipals(cx, script) == principalsA);
        CHECK(obj = JS_GetFunctionObject(fun));
    }

    // Clone into B
    {
        JSAutoEnterCompartment b;
        if (!b.enter(cx, B))
            return false;

        JSObject *cloned;
        CHECK(cloned = JS_CloneFunctionObject(cx, obj, B));

        JSFunction *fun;
        CHECK(fun = JS_ValueToFunction(cx, JS::ObjectValue(*cloned)));

        JSScript *script;
        CHECK(script = JS_GetFunctionScript(cx, fun));

        CHECK(JS_GetScriptPrincipals(cx, script) == principalsB);

        JS::Value v;
        JS::Value args[] = { JS::Int32Value(1) };
        CHECK(JS_CallFunctionValue(cx, B, JS::ObjectValue(*cloned), 1, args, &v));
        CHECK(v.isObject());

        JSObject *funobj = &v.toObject();
        CHECK(JS_ObjectIsFunction(cx, funobj));
        CHECK(fun = JS_ValueToFunction(cx, v));
        CHECK(script = JS_GetFunctionScript(cx, fun));
        CHECK(JS_GetScriptPrincipals(cx, script) == principalsB);
    }

    return true;
}
END_TEST(test_cloneScriptWithPrincipals)
