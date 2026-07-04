"use client";
import { useRef } from "react";
import * as THREE from "three";
import { Canvas, useFrame } from "@react-three/fiber";
import { OrbitControls } from "@react-three/drei";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

function TreeNode({ position, color }: { position: [number, number, number]; color: string }) {
  return (
    <mesh position={position}>
      <boxGeometry args={[1.2, 0.4, 0.4]} />
      <meshStandardMaterial color={color} emissive={color} emissiveIntensity={0.5} />
    </mesh>
  );
}

function Edge({ from, to }: { from: [number, number, number]; to: [number, number, number] }) {
  return (
    <line>
      <bufferGeometry>
        <float32BufferAttribute
          attach="attributes-position"
          args={[new Float32Array([...from, ...to]), 3]}
          count={2}
        />
      </bufferGeometry>
      <lineBasicMaterial color="#4C1D95" transparent opacity={0.5} />
    </line>
  );
}

function AstScene() {
  const groupRef = useRef<THREE.Group>(null);
  const reduced = useReducedMotion();

  useFrame(() => {
    if (groupRef.current && !reduced) {
      groupRef.current.rotation.y += 0.003;
    }
  });

  const nodes = [
    { label: "NODE_LIST", pos: [0, 2, 0] as [number, number, number], color: "#7C3AED" },
    { label: "CMD(cat)", pos: [-1.5, 0, 0] as [number, number, number], color: "#00FF41" },
    { label: "CMD(echo)", pos: [1.5, 0, 0] as [number, number, number], color: "#F43F5E" },
    { label: "REDIR(<<HD)", pos: [-2.5, -2, 0] as [number, number, number], color: "#A78BFA" },
    { label: "ARG(done)", pos: [1.5, -2, 0] as [number, number, number], color: "#F43F5E" },
  ];

  const edges = [
    { from: [0, 1.7, 0] as [number, number, number], to: [-1.5, 0.2, 0] as [number, number, number] },
    { from: [0, 1.7, 0] as [number, number, number], to: [1.5, 0.2, 0] as [number, number, number] },
    { from: [-1.5, -0.2, 0] as [number, number, number], to: [-2.5, -1.8, 0] as [number, number, number] },
    { from: [1.5, -0.2, 0] as [number, number, number], to: [1.5, -1.8, 0] as [number, number, number] },
  ];

  return (
    <group ref={groupRef}>
      <ambientLight intensity={0.3} />
      <pointLight position={[5, 5, 5]} intensity={1} color="#7C3AED" />
      {nodes.map((n) => (
        <TreeNode key={n.label} position={n.pos} color={n.color} />
      ))}
      {edges.map((e, i) => (
        <Edge key={i} {...e} />
      ))}
    </group>
  );
}

export default function AstVisualizer() {
  const isDesktop = useIsDesktop();

  return (
    <section className="relative z-10 py-20 px-4 max-w-5xl mx-auto">
      <div className="text-center mb-8">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">
          /ast
        </h2>
        <p className="font-mono text-sm text-[#A78BFA]/60 max-w-xl mx-auto">
          Every command becomes a node in an Abstract Syntax Tree
        </p>
      </div>

      {isDesktop ? (
        <div className="w-full h-80 md:h-96 neon-border rounded-sm overflow-hidden">
          <Canvas camera={{ position: [0, 0, 8], fov: 50 }} dpr={[1, 1.5]}>
            <AstScene />
            <OrbitControls enableZoom={false} enablePan={false} />
          </Canvas>
        </div>
      ) : (
        <div className="neon-border rounded-sm p-6 bg-[#0F0F23]/80">
          <pre className="font-mono text-xs md:text-sm text-[#00FF41] overflow-x-auto">
{`NODE_LIST
├── CMD(cat)
│   └── REDIR(<<HD)
└── CMD(echo)
    └── ARG(done)`}
          </pre>
        </div>
      )}
    </section>
  );
}
