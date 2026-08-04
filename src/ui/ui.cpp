#include <shiki/ui/ui.h>

namespace shiki {

void UIComponent::onRender() {}

void UIComponent::addChild(std::unique_ptr<UIComponent> child) {
	if (child) {
		child->setParent(this);
		children_.push_back(std::move(child));
	}
}

void UIComponent::removeChild(UIComponent *child) {
	children_.erase(
	    std::remove_if(children_.begin(), children_.end(),
	                   [child](const std::unique_ptr<UIComponent> &ptr) {
		                   return ptr.get() == child;
	                   }),
	    children_.end());
}

void UIText::onRender() {}

void UIButton::onRender() {}

void UIProgressBar::onRender() {}

void UIPanel::onRender() {}

void UIManager::initialize() { clear(); }

void UIManager::shutdown() { clear(); }

void UIManager::addComponent(std::unique_ptr<UIComponent> component) {
	if (component) {
		components_.push_back(std::move(component));
	}
}

void UIManager::update(float dt) {
	for (auto &component : components_) {
		if (component->isVisible() && component->isEnabled()) {
			component->onUpdate(dt);
		}
	}
}

void UIManager::render() {
	for (auto &component : components_) {
		if (component->isVisible()) {
			component->onRender();
		}
	}
}

void UIManager::onMouseMove(float x, float y) {
	UIComponent *newHovered = nullptr;
	for (auto &component : components_) {
		if (component->isVisible() && component->isEnabled()) {
			float left = component->getPosition().x;
			float right = left + component->getSize().x;
			float top = component->getPosition().y;
			float bottom = top + component->getSize().y;

			if (x >= left && x <= right && y >= top && y <= bottom) {
				newHovered = component.get();
				break;
			}
		}
	}

	if (newHovered != hoveredComponent_) {
		if (hoveredComponent_) {
			hoveredComponent_->onLeave();
		}
		if (newHovered) {
			newHovered->onHover();
		}
		hoveredComponent_ = newHovered;
	}
}

void UIManager::onMouseClick(float x, float y) {
	if (hoveredComponent_) {
		hoveredComponent_->onClick();
	}
}

void UIManager::clear() {
	components_.clear();
	hoveredComponent_ = nullptr;
}

} // namespace shiki
