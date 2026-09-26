"use client";
import { useEffect, useState } from "react";

/**
 * WebGL is not available in every browser or VM. Without this check
 * react-three-fiber's <Canvas> throws and takes the whole page down with it,
 * so 3D sections fall back to their 2D/CSS version instead.
 */
export function useWebGL(): boolean {
  const [supported, setSupported] = useState(false);

  useEffect(() => {
    try {
      const canvas = document.createElement("canvas");
      const gl = canvas.getContext("webgl2") || canvas.getContext("webgl");
      setSupported(Boolean(gl));
    } catch {
      setSupported(false);
    }
  }, []);

  return supported;
}