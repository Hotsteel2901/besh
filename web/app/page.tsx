import Hero from "./components/Hero";
import Terminal from "./components/Terminal";
import FeatureSection from "./components/FeatureSection";
import BuiltinsGrid from "./components/BuiltinsGrid";
import AstVisualizer from "./components/AstVisualizer";
import Arcade from "./components/Arcade";
import BenchmarkBar from "./components/BenchmarkBar";
import Footer from "./components/Footer";

export default function Home() {
  return (
    <main className="relative">
      <Hero />
      <Terminal />
      <FeatureSection />
      <BuiltinsGrid />
      <AstVisualizer />
      <BenchmarkBar />
      <Arcade />
      <Footer />
    </main>
  );
}