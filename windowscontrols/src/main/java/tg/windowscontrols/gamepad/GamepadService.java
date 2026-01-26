package tg.windowscontrols.gamepad;

import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWGamepadState;

public class GamepadService {

    private final GLFWGamepadState glfwState = GLFWGamepadState.create();

    public boolean init() {
        if (!GLFW.glfwInit()) {
            return false;
        }
        return GLFW.glfwJoystickIsGamepad(GLFW.GLFW_JOYSTICK_1);
    }

    public byte[] pollCompressed() {
        if (!GLFW.glfwGetGamepadState(GLFW.GLFW_JOYSTICK_1, glfwState)) {
            return null;
        }

        return getCompressedState(glfwState);
    }

    private byte axisToByte(float f) {
        // Clamp to be sure
        if (f > 1f) f = 1f;
        if (f < -1f) f = -1f;

        // convert -100 .. 100
        return (byte) Math.round(f * 100);
    }

    private byte buttonsToByte(boolean a, boolean b, boolean x, boolean y) {
        byte result = 0;
        if (a) result |= 1 << 0; // bit 0
        if (b) result |= 1 << 1; // bit 1
        if (x) result |= 1 << 2; // bit 2
        if (y) result |= 1 << 3; // bit 3
        return result;
    }

    public byte[] getCompressedState(GLFWGamepadState glfwState) {
        byte[] payload = new byte[8]; // 1 type + 6 axes + 1 button byte

        payload[0] = 0; // gamepad type
        payload[1] = axisToByte(glfwState.axes(GLFW.GLFW_GAMEPAD_AXIS_LEFT_X));
        payload[2] = axisToByte(glfwState.axes(GLFW.GLFW_GAMEPAD_AXIS_LEFT_Y));
        payload[3] = axisToByte(glfwState.axes(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_X));
        payload[4] = axisToByte(glfwState.axes(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y));
        payload[5] = axisToByte(glfwState.axes(GLFW.GLFW_GAMEPAD_AXIS_LEFT_TRIGGER));
        payload[6] = axisToByte(glfwState.axes(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER));

        payload[7] = buttonsToByte(
            glfwState.buttons(GLFW.GLFW_GAMEPAD_BUTTON_A) == GLFW.GLFW_PRESS,
            glfwState.buttons(GLFW.GLFW_GAMEPAD_BUTTON_B) == GLFW.GLFW_PRESS,
            glfwState.buttons(GLFW.GLFW_GAMEPAD_BUTTON_X) == GLFW.GLFW_PRESS,
            glfwState.buttons(GLFW.GLFW_GAMEPAD_BUTTON_Y) == GLFW.GLFW_PRESS
        );

        return payload;
    }

}
