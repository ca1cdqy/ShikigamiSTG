#pragma once

#include <functional>
#include <memory>
#include <shiki/core/types.h>
#include <string>
#include <vector>

namespace shiki {

/**
 * Base node in the retained-mode UI tree.
 *
 * A component owns its children and observes its parent. Components are
 * expected to be created, updated, rendered, and destroyed on the thread that
 * owns the UIManager.
 *
 * @thread_safety Not thread-safe.
 */
class UIComponent {
  public:
	/** Creates a visible and enabled component with no parent. */
	UIComponent() = default;
	/** Destroys the component and all owned children. */
	virtual ~UIComponent() = default;

	/** Components cannot be copied because they own child nodes. */
	UIComponent(const UIComponent &) = delete;
	/** Components cannot be copy-assigned. */
	UIComponent &operator=(const UIComponent &) = delete;

	/** Transfers component state and child ownership. */
	UIComponent(UIComponent &&) noexcept = default;
	/** Replaces this component by moving state and child ownership. */
	UIComponent &operator=(UIComponent &&) noexcept = default;

	/** Called when the component enters an initialized UI tree. */
	virtual void onInitialize() {}
	/** Advances component state by one frame. */
	virtual void onUpdate(float dt) {}
	/** Queues the component's visual representation. */
	virtual void onRender();
	/** Called before the component leaves its initialized UI tree. */
	virtual void onDestroy() {}

	/** Sets the component position in parent-local coordinates. */
	void setPosition(const Vec2 &pos) { position_ = pos; }
	/** Sets the component width and height used for layout and hit testing. */
	void setSize(const Vec2 &size) { size_ = size; }
	/** Returns the parent-local position. */
	[[nodiscard]] const Vec2 &getPosition() const { return position_; }
	/** Returns the configured component size. */
	[[nodiscard]] const Vec2 &getSize() const { return size_; }

	/** Controls whether the component participates in rendering. */
	void setVisible(bool visible) { visible_ = visible; }
	/** Reports whether the component is visible. */
	[[nodiscard]] bool isVisible() const { return visible_; }

	/** Controls whether the component accepts interaction. */
	void setEnabled(bool enabled) { enabled_ = enabled; }
	/** Reports whether the component accepts interaction. */
	[[nodiscard]] bool isEnabled() const { return enabled_; }

	/** Sets the observed parent without transferring ownership. */
	void setParent(UIComponent *parent) { parent_ = parent; }
	/** Returns the non-owning parent pointer, or null for a root component. */
	[[nodiscard]] UIComponent *getParent() const { return parent_; }

	/** Adds an owned child and assigns this component as its parent. */
	void addChild(std::unique_ptr<UIComponent> child);
	/** Removes and destroys the matching direct child. */
	void removeChild(UIComponent *child);
	/** Returns the directly owned child collection. */
	[[nodiscard]] const std::vector<std::unique_ptr<UIComponent>> &
	getChildren() const {
		return children_;
	}

	/** Handles a pointer click routed to this component. */
	virtual void onClick() {}
	/** Handles pointer entry. */
	virtual void onHover() {}
	/** Handles pointer exit. */
	virtual void onLeave() {}

  protected:
	/** Position relative to the parent component. */
	Vec2 position_;
	/** Logical width and height used for layout and hit testing. */
	Vec2 size_;
	/** Whether rendering traversals include this component. */
	bool visible_ = true;
	/** Whether interaction traversals include this component. */
	bool enabled_ = true;
	/** Non-owning parent pointer, or null for root components. */
	UIComponent *parent_ = nullptr;
	/** Directly owned child components in traversal order. */
	std::vector<std::unique_ptr<UIComponent>> children_;
};

/** Displays one line of text using the UI rendering backend. */
class UIText : public UIComponent {
  public:
	/** Creates an empty text component. */
	UIText() = default;
	/** Destroys the text component. */
	~UIText() = default;

	/** Replaces the displayed UTF-8 text. */
	void setText(const std::string &text) { text_ = text; }
	/** Sets the font size in logical pixels. */
	void setFontSize(int size) { fontSize_ = size; }
	/** Sets the packed RGBA text color. */
	void setColor(uint32_t color) { color_ = color; }
	/** Sets alignment to 0 for left, 1 for center, or 2 for right. */
	void setAlignment(int alignment) { alignment_ = alignment; }

	/** Returns the displayed text. */
	[[nodiscard]] const std::string &getText() const { return text_; }
	/** Returns the font size in logical pixels. */
	[[nodiscard]] int getFontSize() const { return fontSize_; }
	/** Returns the packed RGBA text color. */
	[[nodiscard]] uint32_t getColor() const { return color_; }

	/** Queues the text for rendering. */
	void onRender() override;

  private:
	std::string text_;
	int fontSize_ = 16;
	uint32_t color_ = 0xFFFFFFFF;
	int alignment_ = 0; // 0=left, 1=center, 2=right
};

/** Clickable text button with one command callback. */
class UIButton : public UIComponent {
  public:
	/** Creates a button with no label or callback. */
	UIButton() = default;
	/** Destroys the button. */
	~UIButton() = default;

	/** Replaces the button label. */
	void setText(const std::string &text) { text_ = text; }
	/** Replaces the callback invoked by onClick(). */
	void setCallback(std::function<void()> callback) {
		callback_ = std::move(callback);
	}

	/** Invokes the configured callback when one is present. */
	void onClick() override {
		if (callback_) {
			callback_();
		}
	}

	/** Queues the button for rendering. */
	void onRender() override;

  private:
	std::string text_;
	std::function<void()> callback_;
	bool isHovered_ = false;
};

/** Displays a normalized progress value as a filled bar. */
class UIProgressBar : public UIComponent {
  public:
	/** Creates an empty progress bar. */
	UIProgressBar() = default;
	/** Destroys the progress bar. */
	~UIProgressBar() = default;

	/** Sets progress after clamping the value to the inclusive range [0, 1]. */
	void setProgress(float progress) {
		progress_ = clamp(progress, 0.0f, 1.0f);
	}
	/** Sets the packed RGBA fill color. */
	void setBarColor(uint32_t color) { barColor_ = color; }
	/** Sets the packed RGBA background color. */
	void setBackgroundColor(uint32_t color) { bgColor_ = color; }

	/** Returns normalized progress in the inclusive range [0, 1]. */
	[[nodiscard]] float getProgress() const { return progress_; }

	/** Queues the progress bar for rendering. */
	void onRender() override;

  private:
	float progress_ = 0.0f;
	uint32_t barColor_ = 0xFF00FF00;
	uint32_t bgColor_ = 0xFF808080;
};

/** Rectangular panel used to group or back other UI components. */
class UIPanel : public UIComponent {
  public:
	/** Creates a panel with default background and border colors. */
	UIPanel() = default;
	/** Destroys the panel. */
	~UIPanel() = default;

	/** Sets the packed RGBA background color. */
	void setBackgroundColor(uint32_t color) { bgColor_ = color; }
	/** Sets the packed RGBA border color. */
	void setBorderColor(uint32_t color) { borderColor_ = color; }
	/** Sets border width in logical pixels. */
	void setBorderWidth(float width) { borderWidth_ = width; }

	/** Queues the panel for rendering. */
	void onRender() override;

  private:
	uint32_t bgColor_ = 0xFF333333;
	uint32_t borderColor_ = 0xFF808080;
	float borderWidth_ = 1.0f;
};

/**
 * Owns root UI components and routes update, render, and pointer events.
 *
 * @thread_safety All operations must run on the owning UI thread.
 */
class UIManager {
  public:
	/** Creates an empty manager. */
	UIManager() = default;
	/** Destroys all remaining root components. */
	~UIManager() = default;

	/** Managers cannot be copied because they own root components. */
	UIManager(const UIManager &) = delete;
	/** Managers cannot be copy-assigned. */
	UIManager &operator=(const UIManager &) = delete;

	/** Transfers root component ownership. */
	UIManager(UIManager &&) noexcept = default;
	/** Replaces this manager by moving root component ownership. */
	UIManager &operator=(UIManager &&) noexcept = default;

	/** Initializes every registered root component. */
	void initialize();
	/** Destroys component runtime state and releases all roots. */
	void shutdown();

	/** Adds an owned root component. */
	void addComponent(std::unique_ptr<UIComponent> component);

	/** Advances enabled components by one frame. */
	void update(float dt);

	/** Renders visible components in tree order. */
	void render();

	/** Updates hover routing for a pointer position in UI coordinates. */
	void onMouseMove(float x, float y);
	/** Routes a click to the topmost enabled component at the position. */
	void onMouseClick(float x, float y);

	/** Removes and destroys all root components. */
	void clear();

  private:
	std::vector<std::unique_ptr<UIComponent>> components_;
	UIComponent *hoveredComponent_ = nullptr;
};

} // namespace shiki
