package org.lsposed.lsplant;

public class LSPTest {
    private static final boolean ALWAYS_FALSE = Boolean.parseBoolean("False".toLowerCase());

    static {
        System.loadLibrary("test");
    }

    boolean field;

    LSPTest() {
        field = false;
    }

    native static boolean initHooker();

    native static void killAfterDelay(long milliseconds);

    static boolean staticMethod() {
        if (ALWAYS_FALSE) {
            try {
                for (int index = 0; index < 100; index++) {
                    staticMethod();
                }
            } catch (Exception exception) {
                throw new RuntimeException(exception);
            }
        }
        return false;
    }

    String normalMethod(String a, int b, long c) {
        if (ALWAYS_FALSE) {
            try {
                for (int index = 0; index < 100; index++) {
                    normalMethod(a, b, c);
                }
            } catch (Exception exception) {
                throw new RuntimeException(exception);
            }
        }
        return a + b + c;
    }

    String manyParametersMethod(String a, boolean b, byte c, short d, int e, long f, float g, double h, Integer i, Long j) {
        if (ALWAYS_FALSE) {
            try {
                for (int index = 0; index < 100; index++) {
                    manyParametersMethod(a, b, c, d, e, f, g, h, i, j);
                }
            } catch (Exception exception) {
                throw new RuntimeException(exception);
            }
        }
        return a + b + c + d + e + f + g + h + i + j;
    }

    static boolean staticMethodWithException() {
        if (ALWAYS_FALSE) {
            try {
                for (int index = 0; index < 100; index++) {
                    staticMethodWithException();
                }
            } catch (Exception exception) {
                throw new RuntimeException(exception);
            }
        }
        throw new RuntimeException("origin");
    }

    public static class NeedInitialize {
        static int x;

        static {
            x = 0;
        }

        static boolean staticMethod() {
            if (ALWAYS_FALSE) {
                try {
                    for (int index = 0; index < 100; index++) {
                        staticMethod();
                    }
                } catch (Exception exception) {
                    throw new RuntimeException(exception);
                }
            }
            try {
                return x != 0;
            } catch (Throwable e) {
                return false;
            }
        }

        static boolean callStaticMethod() {
            if (ALWAYS_FALSE) {
                try {
                    for (int index = 0; index < 100; index++) {
                        callStaticMethod();
                    }
                } catch (Exception exception) {
                    throw new RuntimeException(exception);
                }
            }
            try {
                return staticMethod();
            } catch (Throwable e) {
                return false;
            }
        }
    }

    public static class NeedInitialize2 {
        static int x;

        static {
            x = 0;
        }

        static boolean staticMethod() {
            if (ALWAYS_FALSE) {
                try {
                    for (int index = 0; index < 100; index++) {
                        staticMethod();
                    }
                } catch (Exception exception) {
                    throw new RuntimeException(exception);
                }
            }
            try {
                return x != 0;
            } catch (Throwable e) {
                return false;
            }
        }

        static boolean callStaticMethod() {
            if (ALWAYS_FALSE) {
                try {
                    for (int index = 0; index < 100; index++) {
                        callStaticMethod();
                    }
                } catch (Exception exception) {
                    throw new RuntimeException(exception);
                }
            }
            try {
                return staticMethod();
            } catch (Throwable e) {
                return false;
            }
        }
    }

    public interface ForProxy {
        String abstractMethod(String a, boolean b, byte c, short d, int e, long f, float g, double h, Integer i, Long j);
    }

    public interface ForProxy2 {
        String abstractMethod(String a, boolean b, byte c, short d, int e, long f, float g, double h, Integer i, Long j);
    }
}
