package tg.windowscontrols.model;

public record GamepadState(
        float leftX,
        float leftY,
        float rightX,
        float rightY,
        float leftTrigger,
        float rightTrigger,

        boolean buttonA,
        boolean buttonB,
        boolean buttonX,
        boolean buttonY
) {}
