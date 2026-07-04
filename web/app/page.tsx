import Hero from "./components/Hero";
import Terminal from "./components/Terminal";
import FeatureSection from "./components/FeatureSection";
import BuiltinsGrid from "./components/BuiltinsGrid";
import AstVisualizer from "./components/AstVisualizer";
import BenchmarkBar from "./components/BenchmarkBar";
import PacEat from "./games/PacEat";
import SpeedType from "./games/SpeedType";
import AsciiRain from "./games/AsciiRain";
import TowerDefense from "./games/TowerDefense";
import Footer from "./components/Footer";

export default function Home() {
  return (
    <main className="relative">
      <Hero />
      <Terminal />
      <FeatureSection />
      <BuiltinsGrid />
      <PacEat />
      <TowerDefense />
      <AstVisualizer />
      <AsciiRain />
      <SpeedType />
      <BenchmarkBar />
      <Footer />
    </main>
  );
}
