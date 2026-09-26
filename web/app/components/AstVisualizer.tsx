"use client";
import { useRef } from "react";
import * as THREE from "three";
import { Canvas, useFrame } from "@react-three/fiber";
import { OrbitControls } from "@react-three/drei";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";
import { useInViewport } from "@/app/hooks/useInViewport";
import { useWebGL } from "@/app/hooks/useWebGL";

const TREE = `NODE_LIST
├── CMD(cat)
│   └── REDIR(<<HD)
└── CMD(echo)
    └── ARG(done)`;

const LEGEND = [
  { color: "#A78BFA", label: "list" },
  { color: "#4ADE80", label: "builtin" },
  { color: "#FB7185", label: "external / arg" },
  { color: "#C4B5FD", label: "redirection" },
];

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
    { label: "CMD(cat)", pos: [-1.5, 0, 0] as [number, number, number], color: "#4ADE80" },
    { label: "CMD(echo)", pos: [1.5, 0, 0] as [number, number, number], color: "#FB7185" },
    { label: "REDIR(<<HD)", pos: [-2.5, -2, 0] as [number, number, number], color: "#A78BFA" },
    { label: "ARG(done)", pos: [1.5, -2, 0] as [number, number, number], color: "#FB7185" },
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
  const webgl = useWebGL();
  const { ref, inView } = useInViewport<HTMLElement>(0.2);
  const show3D = isDesktop && webgl;

  return (
    <section id="ast" ref={ref} className="shell section">
      <div className="mb-10 text-center">
        <p className="kicker">/ast</p>
        <h2 className="section-title">Syntax tree</h2>
        <p className="section-sub">
          Every command becomes a node in an abstract syntax tree — this is how{" "}
          <span className="text-[color:var(--green)]">cat &lt;&lt;EOF</span> is parsed.
        </p>
      </div>

      {show3D ? (
        <>
          <div className="panel h-80 overflow-hidden md:h-96">
            {/* rendering stops while the section is off-screen */}
            <Canvas
              camera={{ position: [0, 0, 8], fov: 50 }}
              dpr={[1, 1.5]}
              frameloop={inView ? "always" : "never"}
            >
              <AstScene />
              <OrbitControls enableZoom={false} enablePan={false} />
            </Canvas>
          </div>
          <div className="mt-4 flex flex-wrap items-center justify-center gap-4 font-mono text-xs text-[color:var(--text-3)]">
            <span>drag to rotate</span>
            {LEGEND.map((item) => (
              <span key={item.label} className="flex items-center gap-1.5">
                <span className="inline-block h-2.5 w-2.5 rounded-[2px]" style={{ background: item.color }} />
                {item.label}
              </span>
            ))}
          </div>
        </>
      ) : (
        <div className="panel mx-auto max-w-2xl p-6">
          <pre className="overflow-x-auto font-mono text-xs leading-relaxed text-[color:var(--green)] md:text-sm">
            {TREE}
          </pre>
          {isDesktop && (
            <p className="mt-3 font-mono text-xs text-[color:var(--text-3)]">
              WebGL isn&apos;t available here, so the tree is shown as text.
            </p>
          )}
        </div>
      )}
    </section>
  );
}