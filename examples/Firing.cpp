#include <weapon_library/FiringMechanism.h>

#include <iostream>

namespace {

void consume(const char* label, const std::vector<weapon_library::FiringEvent>& events) {
    for (const auto& event : events) {
        if (const auto* shot = std::get_if<weapon_library::ShotFiredEvent>(&event)) {
            // The game resolves the muzzle pose and creates its hit query/projectile here.
            std::cout << label << ": shot at " << shot->time_seconds
                      << " s, rounds remaining: " << shot->rounds_remaining << '\n';
        }
    }
}

} // namespace

int main() {
    using namespace weapon_library;
    constexpr ActionId primary_attack = 1;
    const auto actions = std::make_shared<const WeaponDefinition>(
        std::vector<ActionDefinition>{{primary_attack, 0.0, 0.0, 0.05}});

    FiringMechanism pistol(actions, {primary_attack, FireMode::SemiAutomatic, 0.2, 12}, 12);
    FiringMechanism rifle(actions, {primary_attack, FireMode::Automatic, 0.1, 30}, 30);
    FiringMechanism turret(actions, {primary_attack, FireMode::Automatic, 0.25, 100}, 100);

    consume("Pistol", pistol.execute(TriggerCommand::Press));
    consume("Pistol", pistol.advance(0.5)); // Holding does not repeat semi-auto.
    pistol.execute(TriggerCommand::Release);
    consume("Pistol", pistol.execute(TriggerCommand::Press));

    consume("Rifle", rifle.execute(TriggerCommand::Press));
    consume("Rifle", rifle.advance(0.5)); // Five more shots within one update.
    rifle.execute(TriggerCommand::Release);

    // A game AI or turret controller supplies the same commands as player input.
    consume("Turret", turret.execute(TriggerCommand::Press));
    consume("Turret", turret.advance(0.5));
    turret.execute(TriggerCommand::Release);

    return pistol.state().rounds_remaining == 10 && rifle.state().rounds_remaining == 24 &&
           turret.state().rounds_remaining == 97 ? 0 : 1;
}
