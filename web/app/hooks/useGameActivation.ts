"use client";
import { useCallback, useEffect, useRef, useState } from "react";

/**
 * Focus/keyboard state machine shared by the keyboard-driven games.
 *
 * Guarantees:
 * - Keyboard listeners only exist while `active` is true, so pages keep
 *   scrolling, Tab focus and other inputs stay usable when the game is idle.
 * - Only keys listed in `gameKeys` are `preventDefault`-ed (and only while
 *   active). Escape always releases the game.
 * - Losing focus of the container (or pressing Escape) deactivates it.
 */
export function useGameActivation(gameKeys: readonly string[] = []) {
  const [active, setActive] = useState(false);
  const containerRef = useRef<HTMLDivElement>(null);
  const pressedRef = useRef<Record<string, boolean>>({});
  const keysRef = useRef(gameKeys);
  keysRef.current = gameKeys;

  const deactivate = useCallback(() => {
    setActive(false);
    pressedRef.current = {};
  }, []);

  const activate = useCallback(() => {
    pressedRef.current = {};
    setActive(true);
    containerRef.current?.focus({ preventScroll: true });
  }, []);

  useEffect(() => {
    if (!active) return;

    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        e.preventDefault();
        deactivate();
        return;
      }
      if (keysRef.current.includes(e.key)) {
        e.preventDefault();
        pressedRef.current[e.key] = true;
      }
    };
    const onKeyUp = (e: KeyboardEvent) => {
      if (keysRef.current.includes(e.key)) {
        e.preventDefault();
        pressedRef.current[e.key] = false;
      }
    };

    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", onKeyUp);
    return () => {
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("keyup", onKeyUp);
    };
  }, [active, deactivate]);

  const handleBlur = useCallback(
    (e: React.FocusEvent<HTMLDivElement>) => {
      const next = e.relatedTarget as Node | null;
      // Focus merely moved between children (e.g. a button inside the game);
      // keep the game active in that case.
      if (next && e.currentTarget.contains(next)) return;
      deactivate();
    },
    [deactivate]
  );

  return { active, activate, deactivate, handleBlur, containerRef, pressedRef };
}