"use client";
import { useRef, useMemo } from "react";
import { Canvas, useFrame } from "@react-three/fiber";
import * as THREE from "three";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

const CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$#@%&*()_+-=[]{}|;:<>?,./";

function RainDrop({ index, total }: { index: number; total: number }) {
  const meshRef = useRef<THREE.Mesh>(null);
  const col = useMemo(() => (index / total) * 30 - 15, [index, total]);
  const speed = useMemo(() => 0.02 + Math.random() * 0.08, []);

  useFrame(() => {
    if (meshRef.current) {
      meshRef.current.position.y -= speed;
      if (meshRef.current.position.y < -10) {
        meshRef.current.position.y = 10;
      }
    }
  });

  return (
    <mesh ref={meshRef} position={[col, Math.random() * 20 - 10, -5]}>
      <planeGeometry args={[0.5, 3]} />
      <meshBasicMaterial color="#00FF41" transparent opacity={0.6} />
    </mesh>
  );
}

function RainScene() {
  const drops = useMemo(() => Array.from({ length: 80 }, (_, i) => i), []);

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
    <div className="absolute inset-0 overflow-hidden pointer-events-none opacity-20">
      {Array.from({ length: 40 }).map((_, i) => (
        <span
          key={i}
          className="absolute text-[#00FF41] text-xs font-mono animate-matrix-fall"
          style={{
            left: `${(i / 40) * 100}%`,
            animationDelay: `${Math.random() * 3}s`,
            animationDuration: `${1 + Math.random() * 2}s`,
          }}
        >
          {CHARS[Math.floor(Math.random() * CHARS.length)]}
        </span>
      ))}
    </div>
  );
}

export default function MatrixRain() {
  const isDesktop = useIsDesktop();
  const reduced = useReducedMotion();

  if (reduced) return null;

  if (isDesktop) {
    return (
      <div className="absolute inset-0 z-0">
        <Canvas camera={{ position: [0, 0, 10], fov: 60 }} dpr={[1, 1.5]}>
          <RainScene />
        </Canvas>
      </div>
    );
  }

  return <CSSRain />;
}
