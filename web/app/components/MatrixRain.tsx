"use client";
import { useEffect, useRef, useState } from "react";
import { Canvas, useFrame } from "@react-three/fiber";
import * as THREE from "three";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";
import { useInViewport } from "@/app/hooks/useInViewport";
import { useWebGL } from "@/app/hooks/useWebGL";

const CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$#@%&*()_+-=[]{}|;:<>?,./";

/* Deterministic pseudo-random so server and client render identical markup
 * (the previous Math.random() version broke hydration). */
function seeded(i: number, salt: number) {
  const x = Math.sin(i * 12.9898 + salt * 78.233) * 43758.5453;
  return x - Math.floor(x);
}

function RainDrop({ index, total }: { index: number; total: number }) {
  const meshRef = useRef<THREE.Mesh>(null);
  const col = (index / total) * 30 - 15;
  const speed = 0.02 + seeded(index, 1) * 0.08;
  const startY = seeded(index, 2) * 20 - 10;

  useFrame(() => {
    if (meshRef.current) {
      meshRef.current.position.y -= speed;
      if (meshRef.current.position.y < -10) {
        meshRef.current.position.y = 10;
      }
    }
  });

  return (
    <mesh ref={meshRef} position={[col, startY, -5]}>
      <planeGeometry args={[0.5, 3]} />
      <meshBasicMaterial color="#00FF41" transparent opacity={0.35} />
    </mesh>
  );
}

function RainScene() {
  const drops = Array.from({ length: 60 }, (_, i) => i);

  return (
    <>
      <ambientLight intensity={0} />
      {drops.map((i) => (
        <RainDrop key={i} index={i} total={drops.length} />
      ))}
    </>
  );
}

function CSSRain() {
  return (
    <div className="absolute inset-0 overflow-hidden opacity-25">
      {Array.from({ length: 28 }).map((_, i) => (
        <span
          key={i}
          className="absolute animate-matrix-fall font-mono text-xs text-[#00FF41]"
          style={{
            left: `${(seeded(i, 3) * 100).toFixed(2)}%`,
            animationDelay: `${(seeded(i, 4) * 3).toFixed(2)}s`,
            animationDuration: `${(1 + seeded(i, 5) * 2).toFixed(2)}s`,
          }}
        >
          {CHARS[Math.floor(seeded(i, 6) * CHARS.length)]}
        </span>
      ))}
    </div>
  );
}

export default function MatrixRain() {
  const isDesktop = useIsDesktop();
  const reduced = useReducedMotion();
  const webgl = useWebGL();
  const { ref, inView } = useInViewport<HTMLDivElement>(0.05);
  const [mounted, setMounted] = useState(false);

  useEffect(() => setMounted(true), []);

  if (reduced) return null;

  /* Cheap CSS rain first; upgrade to the 3D scene once the client has
   * confirmed a desktop viewport and a WebGL context. */
  if (!mounted || !isDesktop || !webgl) return <CSSRain />;

  return (
    <div ref={ref} className="absolute inset-0">
      {/* frameloop stops entirely while the hero is scrolled out of view */}
      <Canvas camera={{ position: [0, 0, 10], fov: 60 }} dpr={[1, 1.5]} frameloop={inView ? "always" : "never"}>
        <RainScene />
      </Canvas>
    </div>
  );
}