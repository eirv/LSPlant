package org.lsposed.lsplant;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

public class Hooker {

    public static class MethodCallback {
        Method backup;
        Object thiz;
        Object[] args;

        MethodCallback(Method backup, Object thiz, Object[] args) {
            this.backup = backup;
            this.thiz = thiz;
            this.args = args;
        }
    }

    public interface HookProvider {
        HookProvider DEFAULT = Hooker::doHook;
        HookProvider NATIVE_API = Hooker::doHookUsingNativeAPI;

        Method doHook(Member target, Object hooker, Method callback);
    }

    public Method backup;

    private Member target;
    private Method replacement;
    private Object owner = null;
    private boolean isStatic;

    private Hooker() {
    }

    private static native Method doHook(Member original, Object hooker, Method callback);

    private static native Method doHookUsingNativeAPI(Member original, Object hooker, Method callback);

    private static native boolean doUnhook(Member target);

    public static native boolean deoptimize(Member target);

    public static native Method findMethod(Class<?> clazz, String name, String signature, boolean isStatic);

    public static native Object invokeSpecial(Member executable, Class<?> declaringClass, Object thiz, Object... args);

    public static Object invoke(Member executable, Object thiz, Object... args) throws InvocationTargetException {
        Class<?> declaringClass = null;
        if (executable instanceof Method method) {
            if ((method.getModifiers() & Modifier.STATIC) != 0) {
                declaringClass = method.getDeclaringClass();
            }
        } else if (executable instanceof Constructor<?> constructor) {
            declaringClass = constructor.getDeclaringClass();
        } else {
            return null;
        }
        try {
            return invokeSpecial(executable, declaringClass, thiz, args);
        } catch (Throwable e) {
            throw new InvocationTargetException(e);
        }
    }

    public Object callback(Object[] packedArgs) throws Throwable {
        Object thiz;
        Object[] args;

        if (isStatic) {
            thiz = null;
            args = packedArgs;
        } else {
            thiz = packedArgs[0];
            args = new Object[packedArgs.length - 1];
            System.arraycopy(packedArgs, 1, args, 0, packedArgs.length - 1);
        }

        var methodCallback = new MethodCallback(backup, thiz, args);
        try {
            return invoke(replacement, owner, methodCallback);
        } catch (InvocationTargetException e) {
            throw e.getTargetException();
        }
    }

    public Object newCallback(Object thiz, Object[] args) throws Throwable {
        var methodCallback = new MethodCallback(backup, thiz, args);
        try {
            return invoke(replacement, owner, methodCallback);
        } catch (InvocationTargetException e) {
            throw e.getTargetException();
        }
    }

    public boolean unhook() {
        return doUnhook(target);
    }

    public static Hooker hook(Member target, Method replacement, Object owner) {
        return hook(target, replacement, owner, HookProvider.DEFAULT);
    }

    public static Hooker hook(Member target, Method replacement, Object owner, HookProvider hookProvider) {
        Hooker hooker = new Hooker();
        try {
            var callbackMethod = hookProvider == HookProvider.DEFAULT
                    ? Hooker.class.getDeclaredMethod("callback", Object[].class)
                    : Hooker.class.getDeclaredMethod("newCallback", Object.class, Object[].class);
            var result = hookProvider.doHook(target, hooker, callbackMethod);
            if (result == null) return null;
            hooker.backup = result;
            hooker.target = target;
            hooker.replacement = replacement;
            hooker.owner = owner;
            hooker.isStatic = (target.getModifiers() & Modifier.STATIC) != 0;
        } catch (NoSuchMethodException ignored) {
        }
        return hooker;
    }
}
