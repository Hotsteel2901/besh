import PacEat from "../games/PacEat";
import TowerDefense from "../games/TowerDefense";
import AsciiRain from "../games/AsciiRain";
import SpeedType from "../games/SpeedType";

export default function Arcade() {
  return (
    <section id="arcade" className="shell section">
      <div className="mb-12 text-center">
        <p className="kicker">/arcade</p>
        <h2 className="section-title">Take a break</h2>
        <p className="section-sub">
          Four small games with a terminal flavour. The canvas ones want a keyboard and a wide
          screen; SpeedType works everywhere.
        </p>
      </div>

      <div className="space-y-16">
        <PacEat />
        <TowerDefense />
        <AsciiRain />
        <SpeedType />
      </div>
    </section>
  );
}