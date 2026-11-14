#include <weapon_library/Weapon.h>

#include <iostream>
#include <memory>
#include <vector>

int main() {
    using namespace weapon_library;
    constexpr ActionId primary_attack = 1;
    const auto definition = std::make_shared<const WeaponDefinition>(
        std::vector<ActionDefinition>{{primary_attack, 0.125, 0.125, 0.25}});
    Weapon weapon(definition);

    auto consume = [](const std::vector<WeaponEvent>& events) {
        for (const auto& event : events) {
            if (event.type == EventType::PhaseEntered && event.phase == Phase::Active) {
                // The game performs its hit query or creates a projectile here.
                std::cout << "Attack became active at " << event.time_seconds << " s\n";
            }
            if (event.type == EventType::ActionCompleted) {
                std::cout << "Weapon became ready at " << event.time_seconds << " s\n";
            }
        }
    };

    const auto result = weapon.execute(WeaponCommand::start(primary_attack));
    if (!result.accepted()) {
        return 1;
    }
    consume(result.events); // Zero windup can activate an attack inside execute().
    consume(weapon.advance(0.5));
    return weapon.state().phase == Phase::Ready ? 0 : 1;
}
