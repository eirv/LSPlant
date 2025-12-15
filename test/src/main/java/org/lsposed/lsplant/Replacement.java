package org.lsposed.lsplant;

import android.util.Log;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

public class Replacement {

    static boolean staticMethodReplacement(Hooker.MethodCallback callback) {
        return true;
    }

    String normalMethodReplacement(Hooker.MethodCallback callback) {
        var a = (String) callback.args[0];
        var b = (int) callback.args[1];
        var c = (long) callback.args[2];
        return a + b + c + "replace";
    }

    String intrinsicMethodReplacement(Hooker.MethodCallback callback) throws InvocationTargetException, IllegalAccessException {
        var sb = (StringBuilder) callback.thiz;
        var out = (String) Hooker.invoke(callback.backup, sb);
        if (out.equals("test")) {
            return "testreplace";
        }
        return out;
    }

    void constructorReplacement(Hooker.MethodCallback callback) throws InvocationTargetException, IllegalAccessException {
        var test = (LSPTest) callback.thiz;
        Hooker.invoke(callback.backup, test);
        test.field = true;
    }

    String manyParametersReplacement(Hooker.MethodCallback callback) {
        var a = (String) callback.args[0];
        var b = (boolean) callback.args[1];
        var c = (byte) callback.args[2];
        var d = (short) callback.args[3];
        var e = (int) callback.args[4];
        var f = (long) callback.args[5];
        var g = (float) callback.args[6];
        var h = (double) callback.args[7];
        var i = (Integer) callback.args[8];
        var j = (Long) callback.args[9];
        return a + b + c + d + e + f + g + h + i + j + "replace";
    }

    static boolean staticMethodWithExceptionReplacement(Hooker.MethodCallback callback) {
        var e = new RuntimeException("replace");
        e.printStackTrace();
        throw e;
    }

    String manyParametersWithExceptionReplacement(Hooker.MethodCallback callback) {
        var a = (String) callback.args[0];
        var b = (boolean) callback.args[1];
        var c = (byte) callback.args[2];
        var d = (short) callback.args[3];
        var e = (int) callback.args[4];
        var f = (long) callback.args[5];
        var g = (float) callback.args[6];
        var h = (double) callback.args[7];
        var i = (Integer) callback.args[8];
        var j = (Long) callback.args[9];
        var exception = new RuntimeException(a + b + c + d + e + f + g + h + i + j + "replace");
        exception.printStackTrace();
        throw exception;
    }
}
