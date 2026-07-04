import type { Metadata } from "next";
import HUDOverlay from "./components/HUDOverlay";
import "./globals.css";

export const metadata: Metadata = {
  title: "besh — Cyber Shell",
  description: "A bash-compatible shell written in C",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en" className="dark">
      <body className="bg-[#0F0F23] text-[#E2E8F0] font-mono">
        {children}
        <HUDOverlay />
      </body>
    </html>
  );
}
