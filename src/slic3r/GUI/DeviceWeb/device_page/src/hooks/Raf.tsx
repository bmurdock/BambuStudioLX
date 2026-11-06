import { useRef, useEffect } from "react";

export function useRafCallback<T extends (...args: unknown[]) => void>(cb: T): T {
    const cbRef = useRef(cb);
    cbRef.current = cb;

    const rafId = useRef<number | null>(null);

    function wrapped(this: ThisParameterType<T>, ...args: Parameters<T>) {
        if (rafId.current != null) return;
        rafId.current = requestAnimationFrame(() => {
            rafId.current = null;
            cbRef.current.apply(this, args);
        });
    }

    useEffect(() => {
        return () => {
            if (rafId.current != null) {
                cancelAnimationFrame(rafId.current);
                rafId.current = null;
            }
        };
    }, []);

    return wrapped as T;
}
