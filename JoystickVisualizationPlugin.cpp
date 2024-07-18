#include "pch.h"
#include "JoystickVisualizationPlugin.h"

BAKKESMOD_PLUGIN(JoystickVisualizationPlugin, "Joystick Visualization", plugin_version, PLUGINTYPE_FREEPLAY);

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

constexpr auto JOYSTICK_VIS_ENABLED = "joystick_vis_enabled";
constexpr auto JOYSTICK_VIS_POINT_COUNT = "joystick_vis_point_count";
constexpr auto JOYSTICK_VIS_SIZE = "joystick_vis_size";
constexpr auto JOYSTICK_VIS_SENSITIVITY = "joystick_vis_sensitivity";
constexpr auto JOYSTICK_VIS_CLAMP = "joystick_vis_clamp";
constexpr auto JOYSTICK_VIS_POINT_SIZE = "joystick_vis_point_size";
constexpr auto JOYSTICK_VIS_CENTER_X = "joystick_vis_center_x";
constexpr auto JOYSTICK_VIS_CENTER_Y = "joystick_vis_center_y";
constexpr auto JOYSTICK_VIS_COLOR_BOX = "joystick_vis_color_box";
constexpr auto JOYSTICK_VIS_COLOR_POINT = "joystick_vis_color_point";
constexpr auto JOYSTICK_VIS_COLOR_DEADZONE = "joystick_vis_color_deadzone";


void JoystickVisualizationPlugin::onLoad() {
	gameWrapper->HookEventWithCaller<CarWrapper>(
		"Function TAGame.Car_TA.SetVehicleInput",
		[this](CarWrapper cw, void* params, std::string eventname) {
			OnSetInput((ControllerInput*)params);
		}
	);

	gameWrapper->RegisterDrawable([this](CanvasWrapper canvas) { Render(canvas); });

	enabled = std::make_shared<bool>(true);
	numberOfPoints = std::make_shared<int>(120);
	boxSize = std::make_shared<int>(400);
	useSensitivity = std::make_shared<bool>(true);
	clampInput = std::make_shared<bool>(true);
	pointPercentage = std::make_shared<float>(0.02);
	centerX = std::make_shared<float>(0.5f);
	centerY = std::make_shared<float>(0.5f);
	boxColor = std::make_shared<LinearColor>();
	pointColor = std::make_shared<LinearColor>();
	pointColorDeadzone = std::make_shared<LinearColor>();

	cvarManager->registerCvar(JOYSTICK_VIS_ENABLED, "1", "Show Joystick Visualization", true, true, 0, true, 1)
		.bindTo(enabled);
	cvarManager->registerCvar(JOYSTICK_VIS_POINT_COUNT, "120", "Number of Inputs to Visualize", true, true, 1)
		.addOnValueChanged(
			[this](std::string oldValue, CVarWrapper cvar) {
				int newNumberOfPoints = cvar.getIntValue();
				*numberOfPoints = newNumberOfPoints;
				inputHistory.reserve(newNumberOfPoints);
			}
		);
	cvarManager->registerCvar(JOYSTICK_VIS_SIZE, "400", "Joystick Visualization Size", true, true, 100, true, 1000)
		.bindTo(boxSize);
	cvarManager->registerCvar(JOYSTICK_VIS_SENSITIVITY, "1", "Scale Input with Aerial Sensitivity", true, true, 0, true, 1)
		.bindTo(useSensitivity);
	cvarManager->registerCvar(JOYSTICK_VIS_CLAMP, "1", "Clamp Values to Max Input", true, true, 0, true, 1)
		.bindTo(clampInput);
	cvarManager->registerCvar(JOYSTICK_VIS_POINT_SIZE, "0.015", "Size of Points Relative to Box", true, true, 0.0f, true, 0.1f)
		.bindTo(pointPercentage);
	cvarManager->registerCvar(JOYSTICK_VIS_CENTER_X, "0.5", "Center of the Visualization - X", true, true, 0.0f, true, 1.0f)
		.bindTo(centerX);
	cvarManager->registerCvar(JOYSTICK_VIS_CENTER_Y, "0.5", "Center of the Visualization - Y", true, true, 0.0f, true, 1.0f)
		.bindTo(centerY);
	cvarManager->registerCvar(JOYSTICK_VIS_COLOR_BOX, "#FFFFFF64", "Box Color")
		.bindTo(boxColor);
	cvarManager->registerCvar(JOYSTICK_VIS_COLOR_POINT, "#FFFFFF", "Point Color")
		.bindTo(pointColor);
	cvarManager->registerCvar(JOYSTICK_VIS_COLOR_DEADZONE, "#FFFFFF", "Point Color in Deadzone")
		.bindTo(pointColorDeadzone);

	inputHistory.reserve(*numberOfPoints);
}

void JoystickVisualizationPlugin::onUnload() {
	gameWrapper->UnregisterDrawables();
}

bool JoystickVisualizationPlugin::isActive() {
	return (gameWrapper->IsInFreeplay() || gameWrapper->IsInCustomTraining())
		&& !gameWrapper->IsPaused()
		&& *enabled;
}

void JoystickVisualizationPlugin::OnSetInput(ControllerInput* ci) {
	if (!isActive()) {
		return;
	}

	if (inputHistory.size() >= *numberOfPoints) {
		int numToRemove = 1 + inputHistory.size() - *numberOfPoints;
		inputHistory.erase(inputHistory.begin(), inputHistory.begin() + numToRemove);
	}

	inputHistory.push_back(*ci);
}

void JoystickVisualizationPlugin::Render(CanvasWrapper canvas) {
	if (!isActive()) {
		return;
	}

	float pointSize = (*boxSize) * (*pointPercentage);
	float boxSizeWithPadding = (*boxSize) + pointSize;

	Vector2 canvasSize = canvas.GetSize();
	Vector2F canvasCenter = Vector2F(
		std::lerp(0, canvasSize.X - boxSizeWithPadding, *centerX),
		std::lerp(0, canvasSize.Y - boxSizeWithPadding, *centerY)
	) + (boxSizeWithPadding / 2.0f);

	canvas.SetColor(*boxColor);
	canvas.SetPosition(Vector2F(canvasCenter.X - boxSizeWithPadding / 2.0f, canvasCenter.Y - boxSizeWithPadding / 2.0f));
	canvas.DrawBox(Vector2F(boxSizeWithPadding, boxSizeWithPadding));

	float scale = *useSensitivity
		? gameWrapper->GetSettings().GetGamepadSettings().AirControlSensitivity
		: 1;

	int i = 0;
	Vector2F prevPos;

	for (auto iter = inputHistory.rbegin(); iter != inputHistory.rend(); ++iter)
	{
		ControllerInput controllerInput = *iter;

		Vector2F rawInput = Vector2F(controllerInput.Steer, controllerInput.Pitch) * scale;
		Vector2F clampedInput = Vector2F(std::clamp(rawInput.X, -1.0f, 1.0f), std::clamp(rawInput.Y, -1.0f, 1.0f));
		Vector2F input = *clampInput ? clampedInput : rawInput;
		Vector2F currentPos = canvasCenter + input * (*boxSize) / 2.0f;

		float opacity = 1 - i / (float)(*numberOfPoints);
		bool isInDeadzone = input.X == 0.0f || input.Y == 0.0f;
		LinearColor color = isInDeadzone ? *pointColorDeadzone : *pointColor;
		color.A *= opacity;
		canvas.SetColor(color);
		Vector2F offset = Vector2F(pointSize / 2.0, pointSize / 2.0);
		canvas.SetPosition(currentPos - offset);
		canvas.FillBox(Vector2F(pointSize, pointSize));

		if (i > 0) {
			canvas.DrawLine(currentPos, prevPos);
		}

		prevPos = currentPos;
		i++;
	}
}

static void Separator() {
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
}

void JoystickVisualizationPlugin::RenderSettings() {
	CVarWrapper enableCvar = cvarManager->getCvar(JOYSTICK_VIS_ENABLED);
	if (!enableCvar) { return; }
	bool enabled = enableCvar.getBoolValue();
	if (ImGui::Checkbox("Show Visualization", &enabled)) {
		enableCvar.setValue(enabled);
	}

	CVarWrapper pointCountCVar = cvarManager->getCvar(JOYSTICK_VIS_POINT_COUNT);
	if (!pointCountCVar) { return; }
	int pointCount = pointCountCVar.getIntValue();
	if (ImGui::SliderInt("Number of Inputs to Visualize", &pointCount, 1, 600)) {
		pointCountCVar.setValue(pointCount);
	}

	CVarWrapper sensitivityCvar = cvarManager->getCvar(JOYSTICK_VIS_SENSITIVITY);
	if (!sensitivityCvar) { return; }
	bool sensitivity = sensitivityCvar.getBoolValue();
	if (ImGui::Checkbox("Scale Input with Aerial Sensitivity", &sensitivity)) {
		sensitivityCvar.setValue(sensitivity);
	}

	CVarWrapper clampCvar = cvarManager->getCvar(JOYSTICK_VIS_CLAMP);
	if (!clampCvar) { return; }
	bool clamp = clampCvar.getBoolValue();
	if (ImGui::Checkbox("Clamp Values to Max Input", &clamp)) {
		clampCvar.setValue(clamp);
	}

	CVarWrapper colorBoxCvar = cvarManager->getCvar(JOYSTICK_VIS_COLOR_BOX);
	if (!colorBoxCvar) { return; }
	LinearColor colorBox = colorBoxCvar.getColorValue() / 255;
	if (ImGui::ColorEdit4("Box Color", &colorBox.R, ImGuiColorEditFlags_AlphaBar)) {
		colorBoxCvar.setValue(colorBox * 255);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset##BoxColor")) {
		colorBoxCvar.setValue(LinearColor(255, 255, 255, 100));
	}

	CVarWrapper colorPointCvar = cvarManager->getCvar(JOYSTICK_VIS_COLOR_POINT);
	if (!colorPointCvar) { return; }
	LinearColor colorPoint = colorPointCvar.getColorValue() / 255;
	if (ImGui::ColorEdit4("Point Color", &colorPoint.R, ImGuiColorEditFlags_AlphaBar)) {
		colorPointCvar.setValue(colorPoint * 255);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset##PointColor")) {
		colorPointCvar.setValue(LinearColor(255, 255, 255, 255));
	}

	CVarWrapper colorDeadzoneCvar = cvarManager->getCvar(JOYSTICK_VIS_COLOR_DEADZONE);
	if (!colorDeadzoneCvar) { return; }
	LinearColor colorDeadzone = colorDeadzoneCvar.getColorValue() / 255;
	if (ImGui::ColorEdit4("Point Color in Deadzone", &colorDeadzone.R, ImGuiColorEditFlags_AlphaBar)) {
		colorDeadzoneCvar.setValue(colorDeadzone * 255);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset##DeadzoneColor")) {
		colorDeadzoneCvar.setValue(colorPointCvar.getColorValue());
	}

	Separator();
	ImGui::Text("Sizing");

	CVarWrapper sizeCvar = cvarManager->getCvar(JOYSTICK_VIS_SIZE);
	if (!sizeCvar) { return; }
	int size = sizeCvar.getIntValue();
	if (ImGui::SliderInt("Visualization Size", &size, 100, 1000)) {
		sizeCvar.setValue(size);
	}

	CVarWrapper centerXCvar = cvarManager->getCvar(JOYSTICK_VIS_CENTER_X);
	if (!centerXCvar) { return; }
	float centerX = centerXCvar.getFloatValue() * 100;
	if (ImGui::SliderFloat("Center X", &centerX, 0.0f, 100.0f, "%.1f %%")) {
		centerXCvar.setValue(centerX / 100.0f);
	}
	ImGui::SameLine();
	if (ImGui::Button("Center##X")) {
		centerXCvar.setValue(0.5f);
	}

	CVarWrapper centerYCvar = cvarManager->getCvar(JOYSTICK_VIS_CENTER_Y);
	if (!centerYCvar) { return; }
	float centerY = centerYCvar.getFloatValue() * 100;
	if (ImGui::SliderFloat("Center Y", &centerY, 0.0f, 100.0f, "%.1f %%")) {
		centerYCvar.setValue(centerY / 100.0f);
	}
	ImGui::SameLine();
	if (ImGui::Button("Center##Y")) {
		centerYCvar.setValue(0.5f);
	}

	CVarWrapper pointSizeCvar = cvarManager->getCvar(JOYSTICK_VIS_POINT_SIZE);
	if (!pointSizeCvar) { return; }
	float pointSize = pointSizeCvar.getFloatValue() * 100;
	if (ImGui::SliderFloat("Size of Points Relative to Box", &pointSize, 0.0f, 10.0f, "%.1f %%")) {
		pointSizeCvar.setValue(pointSize / 100.0f);
	}

	Separator();

	ImGui::Text("Tips");
	ImGui::Text(
		"- Joystick should trace smooth and consistent lines.\n"
		"- Jagged / inconsistent traces may indicate controller needs to be repaired or replaced. Try using compressed air to clean out the sensor.\n"
		"- Try rotating the joystick all the way around edge in a circle.\n"
		"- The default deadzone shape and sensitivity should trace a circle.\n"
		"- Setting a higher sensitivity with an external tool (such as DS4Win or Steam controller config) will increase the size of circle and cut off edges.\n"
		"- A squared deadzone (configured in external tools) should instead trace a square.\n"
		"- The visualization ignores inputs within the deadzone. Only inputs affecting the movement of the car are shown. This is a limitation in Bakkesmod.\n"
		"- See plugin homepage for examples and more information�."
	);

	Separator();

	ImGui::Text("Originally created by @AlpacaFlightSim, updated by @Brotzeitsepp.");
}
