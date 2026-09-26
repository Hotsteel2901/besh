"use client";
import { useCallback, useEffect, useState } from "react";

/**
 * Reports whether the observed element is currently intersecting the viewport.
 * Shared by the games so they can pause their rAF loops / timers while offscreen.
 *
 * Uses a callback ref (instead of an object ref) so it also works for elements
 * that only mount later — e.g. the desktop-only game sections, whose `<section>`
 * is not rendered on the first pass while `useIsDesktop` is still false.
 */
export function useInViewport<T extends Element = HTMLElement>(threshold = 0.15) {
  const [node, setNode] = useState<T | null>(null);
  const [inView, setInView] = useState(true);

  const ref = useCallback((el: T | null) => setNode(el), []);

  useEffect(() => {
    if (!node || typeof IntersectionObserver === "undefined") return;

    const observer = new IntersectionObserver(
      (entries) => {
        const entry = entries[0];
        if (entry) setInView(entry.isIntersecting);
      },
      { threshold }
    );
    observer.observe(node);
    return () => observer.disconnect();
  }, [node, threshold]);

  return { ref, inView };
}