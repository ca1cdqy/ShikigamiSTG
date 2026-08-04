bool switchToSubroutine(int32_t calledSubId, bool saveCurrent = false,
                        int32_t intArg = 0, float floatArg = 0.0f,
                        bool passArguments = false) {
	if (!eclParser || calledSubId < 0)
		return false;
	const auto *calledSub =
	    eclParser->getSubroutine(static_cast<size_t>(calledSubId));
	if (!calledSub)
		return false;
	if (saveCurrent) {
		callStack.push_back({sub, instrIndex + 1, elapsed, waitTimer, vars,
		                     compareRegister, repeatingExInstruction});
	} else {
		callStack.clear();
	}
	sub = calledSub;
	currentSubId = calledSubId;
	instrIndex = 0;
	elapsed = 0.0f;
	waitTimer = 0.0f;
	// CallEclSub in TH06 only changes the instruction pointer, sub id and
	// timer. The complete variable context (including funcSetFunc) remains
	// visible to the callee. A CALL instruction then explicitly overwrites
	// var0/float0 with its two arguments.
	if (passArguments) {
		vars.getInt(-10001) = intArg;
		vars.getFloat(-10005) = floatArg;
	}
	return true;
}

bool triggerInterrupt(int32_t interruptId) {
	const auto found = interrupts.find(interruptId);
	if (found == interrupts.end())
		return false;
	return switchToSubroutine(found->second, true);
}

bool handleDamageCallbacks() {
	if (lifeCallbackThreshold >= 0 && hp < lifeCallbackThreshold) {
		hp = lifeCallbackThreshold;
		const int callback = lifeCallbackSub;
		lifeCallbackThreshold = -1;
		timerCallbackSub = deathCallbackSub;
		if (switchToSubroutine(callback)) {
			phaseTransitionPending = true;
			return true;
		}
	}
	if (hp <= 0 && deathCallbackSub >= 0) {
		const int callback = deathCallbackSub;
		deathCallbackSub = -1;
		lifeCallbackThreshold = -1;
		timerCallbackThreshold = -1;
		// EnemyManager resolves death mode 3 before entering the death
		// callback. Flandre's final cleanup relies on the mode being reset so
		// its later LIFE(0) releases the occupied boss slot instead of
		// preserving it for the 3000-frame tail of the subroutine.
		if (deathMode == 3) {
			hp = 1;
			damageable = false;
			deathMode = 0;
			const bool bossEnemy = isBoss || bossId >= 0;
			if (onBossChanged && bossEnemy)
				onBossChanged(false);
			if (onSpawnEffect) {
				onSpawnEffect(deathAnm1, x, y, 1, 0xffffffff);
				onSpawnEffect(deathAnm1, x, y, 1, 0xffffffff);
				onSpawnEffect(deathAnm1, x, y, 1, 0xffffffff);
			}
		} else {
			hp = 1;
		}
		const bool switched = switchToSubroutine(callback);
		phaseTransitionPending = switched;
		return switched;
	}
	return false;
}

void notifyDeath(bool bossEnemy, int itemState = 0) {
	if (onSpawnEffect) {
		onSpawnEffect(deathAnm1, x, y, 1, 0xffffffff);
		onSpawnEffect(deathAnm2 + 4, x, y, 4, 0xffffffff);
	}
	if (itemDrop != TH06_ITEM_NONE && onDropItem)
		onDropItem(itemDrop, x, y, itemState);
	if (onDeath)
		onDeath(bossEnemy);
}

bool finishDeath(bool bossEnemy) {
	if (deathResolved)
		return true;
	deathResolved = true;
	switch (deathMode) {
	case 3:
		hp = 1;
		damageable = false;
		deathMode = 0;
		if (onBossChanged && bossEnemy)
			onBossChanged(false);
		if (onSpawnEffect) {
			onSpawnEffect(deathAnm1, x, y, 1, 0xffffffff);
			onSpawnEffect(deathAnm1, x, y, 1, 0xffffffff);
			onSpawnEffect(deathAnm1, x, y, 1, 0xffffffff);
		}
		return true;
	case 1:
	case 2:
		// These modes preserve the enemy slot so its ECL can finish the
		// disappearance animation or run a death callback.
		interactable = false;
		damageable = false;
		notifyDeath(false);
		return true;
	default:
		alive = false;
		notifyDeath(bossEnemy);
		return true;
	}
}

bool applyDebugDamage(int damage) {
	if (!alive || damage <= 0)
		return false;
	// Debug damage deliberately bypasses the normal per-frame player damage
	// cap, but uses the same ECL life/death callback chain as a real hit.
	hp = std::max(hp - damage, -damage);
	if (onDamage)
		onDamage();
	if (handleDamageCallbacks())
		return true;
	if (hp <= 0) {
		// A boss phase is owned by its ECL life/death callback.  In the
		// short setup window between phases those callbacks may not have
		// been installed yet; killing the entity here would bypass the
		// remaining script (most visibly at Flandre's Cranberry Trap). Keep
		// the boss alive until ECL exposes the callback that ends the
		// current phase.
		const bool bossEnemy =
		    isBoss || bossId >= 0 || subId == 16 || subId == 31;
		if (bossEnemy) {
			hp = 1;
			return false;
		}
		return finishDeath(false);
	}
	return false;
}

bool runReadyEclInstructions() {
	size_t instructionBudget = 10000;
	while (instrIndex < sub->instructions.size() && instructionBudget > 0) {
		--instructionBudget;
		const auto &instr = sub->instructions[instrIndex];
		float instrTime = static_cast<float>(instr.time) / 60.0f;

		if (waitTimer > 0) {
			break;
		}

		if (instrTime > elapsed) {
			break;
		}

		const uint8_t rankMask = static_cast<uint8_t>(instr.rankMask >> 8);
		if (rankMask != 0 && rankMask != 0xFF &&
		    (rankMask & (1u << difficulty)) == 0) {
			++instrIndex;
			continue;
		}

		const auto *executingSub = sub;
		const size_t executingIndex = instrIndex;
		executeInstr(instr);
		if (sub == executingSub && instrIndex == executingIndex) {
			++instrIndex;
		}
	}
	if (instructionBudget == 0) {
		spdlog::error("ECL sub {} exceeded the per-frame instruction budget",
		              subId);
		alive = false;
		return false;
	}
	return true;
}

// EnemyManager::SpawnEnemy executes time-0 ECL before exposing the enemy to
// collision. Do not advance movement or any of the per-frame timers here.
void initializeEcl() {
	if (!alive || !sub)
		return;
	runReadyEclInstructions();
	sprite.setPosition(x, y);
}

void stepLogic(float dt) {
	const bool enteredAfterPhaseTransition = phaseTransitionPending;
	if (handleDamageCallbacks())
		return;

	if (!globalTimeStopped)
		++bossTimerFrames;
	if (timerCallbackThreshold >= 0 &&
	    bossTimerFrames >= timerCallbackThreshold) {
		if (spellActive && !timeoutSpell && onSpellCaptureFailed)
			onSpellCaptureFailed();
		if (lifeCallbackThreshold > 0) {
			hp = lifeCallbackThreshold;
			lifeCallbackThreshold = -1;
		}
		const int callback = timerCallbackSub;
		timerCallbackThreshold = -1;
		timerCallbackSub = deathCallbackSub;
		bossTimerFrames = 0;
		if (switchToSubroutine(callback))
			return;
	}

	if (!runReadyEclInstructions())
		return;

	// TH06 evaluates death after running ECL as well as after player
	// damage. The final Flandre cleanup subroutine sets life to zero
	// itself, so it must not depend on another player bullet arriving to
	// despawn the boss.
	if (hp <= 0 && deathCallbackSub < 0 && lifeCallbackThreshold < 0) {
		const bool bossEnemy =
		    isBoss || bossId >= 0 || subId == 16 || subId == 31;
		finishDeath(bossEnemy);
		return;
	}

	if (waitTimer > 0) {
		waitTimer -= dt;
		if (waitTimer < 0)
			waitTimer = 0;
	}

	while (sub && instrIndex >= sub->instructions.size() && waitTimer <= 0 &&
	       !callStack.empty()) {
		auto &saved = callStack.back();
		sub = saved.savedSub;
		instrIndex = saved.savedInstrIndex;
		elapsed = saved.savedElapsed;
		waitTimer = saved.savedWaitTimer;
		vars = saved.savedVars;
		compareRegister = saved.savedCompareRegister;
		repeatingExInstruction = saved.savedRepeatingExInstruction;
		callStack.pop_back();
	}
	if (behaviorTimer_ > 0) {
		behaviorTimer_--;
		if (behaviorTimer_ == 0 && !callStack.empty()) {
			auto &saved = callStack.back();
			sub = saved.savedSub;
			instrIndex = saved.savedInstrIndex;
			elapsed = saved.savedElapsed;
			waitTimer = saved.savedWaitTimer;
			vars = saved.savedVars;
			compareRegister = saved.savedCompareRegister;
			repeatingExInstruction = saved.savedRepeatingExInstruction;
			callStack.pop_back();
		}
	}

	applyMovement(dt);
	for (size_t index = 0; index < spellEffectCount; ++index) {
		auto &effect = spellEffects[index];
		if (effect.distance < effect.targetDistance)
			effect.distance += 0.3f;
		effect.angle =
		    std::remainder(effect.angle + std::numbers::pi_v<float> / 100.0f,
		                   std::numbers::pi_v<float> * 2.0f);
		++effect.ageFrames;
	}
	updateDirectionalAnimation();
	updateLasers();
	if (repeatingExInstruction >= 0)
		runExInstruction(repeatingExInstruction);
	if (primaryAnmVm.file)
		primaryAnmVm.tick();

	if (shootInterval > 0 && bulletPattern.configured && inFiring) {
		shootTimer += dt;
		while (shootTimer >= shootInterval) {
			shootTimer -= shootInterval;
			spawnBulletPattern();
		}
	}
	if (enteredAfterPhaseTransition)
		phaseTransitionPending = false;
}

void updateLasers() {
	for (auto &laser : lasers) {
		if (!laser.active)
			continue;

		laser.endOffset += laser.speed;
		if (laser.startLength < laser.endOffset - laser.startOffset)
			laser.startOffset = laser.endOffset - laser.startLength;
		laser.startOffset = std::max(laser.startOffset, 0.0f);

		const float length =
		    std::max(laser.endOffset - laser.startOffset, 0.0f);
		const float centerOffset = (laser.startOffset + laser.endOffset) * 0.5f;
		laser.sprite.setPosition(laser.x + std::cos(laser.angle) * centerOffset,
		                         laser.y +
		                             std::sin(laser.angle) * centerOffset);
		if (const auto texture = laser.sprite.getTexture()) {
			const float textureWidth = static_cast<float>(texture->getWidth());
			const float textureHeight =
			    static_cast<float>(texture->getHeight());
			float widthScale = laser.width / std::max(textureWidth, 1.0f);
			if (laser.state == 0 && laser.startTime > 0) {
				// TH06 keeps a 1.2 px warning line until the last (at most)
				// 30 startup frames, then expands it using age/startTime.
				const int expansionFrames = std::min(laser.startTime, 30);
				const float visibleWidth =
				    laser.stateTimer > laser.startTime - expansionFrames
				        ? static_cast<float>(laser.stateTimer) * laser.width /
				              static_cast<float>(laser.startTime)
				        : 1.2f;
				widthScale = visibleWidth / std::max(textureWidth, 1.0f);
			}
			if (laser.state == 2 && laser.despawnDuration > 0)
				widthScale =
				    (laser.width - static_cast<float>(laser.stateTimer) *
				                       laser.width / laser.despawnDuration) /
				    std::max(textureWidth, 1.0f);
			laser.sprite.setScale(widthScale,
			                      length / std::max(textureHeight, 1.0f));
		}
		laser.sprite.setRotation(
		    laser.angle * 180.0f / std::numbers::pi_v<float> - 90.0f);
		if (laser.flareSprite.getTexture()) {
			laser.flareSprite.setPosition(
			    laser.x + std::cos(laser.angle) * laser.startOffset,
			    laser.y + std::sin(laser.angle) * laser.startOffset);
			float flareScale =
			    (laser.width / 10.0f) * ((16.0f - laser.startOffset) / 16.0f);
			if (flareScale < 0.0f)
				flareScale = laser.width / 10.0f;
			laser.flareSprite.setScale(flareScale, flareScale);
		}

		if (laser.state == 0 && laser.stateTimer >= laser.startTime) {
			laser.state = 1;
			laser.stateTimer = 0;
		} else if (laser.state == 1 && laser.stateTimer >= laser.duration) {
			laser.state = 2;
			laser.stateTimer = 0;
			laser.cancelFrames = 0;
			if (laser.despawnDuration == 0) {
				laser.active = false;
				continue;
			}
		} else if (laser.state == 2 &&
		           laser.stateTimer >= laser.despawnDuration) {
			laser.active = false;
			continue;
		}
		++laser.stateTimer;
		if (laser.state == 2)
			laser.cancelFrames = laser.stateTimer;
		++laser.ageFrames;
	}
}

void applyMovement(float dt) {
	if (movementMode == 2) {
		moveInterpTimer -= 1.0f;
		if (moveInterpTimer < 0.0f)
			moveInterpTimer = 0.0f;
		float t = (moveInterpDuration > 0.0f)
		              ? (moveInterpTimer / moveInterpDuration)
		              : 0.0f;
		if (t < 0.0f)
			t = 0.0f;
		float ease;
		switch (movementEaseType) {
		case 0:
			ease = 1.0f - t;
			break;
		case 1:
			ease = 1.0f - t * t;
			break;
		case 2: {
			float t2 = t * t;
			ease = 1.0f - t2 * t2;
		} break;
		case 3: {
			float u = 1.0f - t;
			ease = u * u;
		} break;
		case 4: {
			float u = 1.0f - t;
			float u2 = u * u;
			ease = u2 * u2;
		} break;
		default:
			ease = 1.0f - t;
			break;
		}
		float targetX = moveInterpStartX + moveInterpX;
		float targetY = moveInterpStartY + moveInterpY;
		float desiredX = ease * moveInterpX + moveInterpStartX;
		float desiredY = ease * moveInterpY + moveInterpStartY;
		vx = desiredX - x;
		vy = desiredY - y;
		x += invertX ? -vx : vx;
		y += vy;
		if (moveInterpTimer <= 0.0f) {
			x = targetX;
			y = targetY;
			vx = 0.0f;
			vy = 0.0f;
			movementMode = 0;
		}
	} else if (movementMode == 1) {
		angle += angularVelocity;
		speed += linearAccel;
		vx = speed * std::cos(angle);
		vy = speed * std::sin(angle);
		x += invertX ? -vx : vx;
		y += vy;
	} else {
		x += invertX ? -vx : vx;
		y += vy;
		vx += ax;
		vy += ay;
	}
	if (shouldClampPos) {
		if (x < boundMinX)
			x = boundMinX;
		if (x > boundMaxX)
			x = boundMaxX;
		if (y < boundMinY)
			y = boundMinY;
		if (y > boundMaxY)
			y = boundMaxY;
	}
}

void update(float dt) {
	if (!alive || !sub)
		return;

	logicAccum += dt;
	while (logicAccum >= LOGIC_DT) {
		logicAccum -= LOGIC_DT;
		stepLogic(static_cast<float>(LOGIC_DT));
		elapsed += static_cast<float>(LOGIC_DT);
	}

	if (primaryAnmVm.file && primaryAnmVm.sprite >= 0) {
		setSpriteIndex(primaryAnmVm.sprite);
		sprite.setScale(primaryAnmVm.scaleX, primaryAnmVm.scaleY);
		sprite.setRotation(-primaryAnmVm.rotation * 180.0f /
		                   std::numbers::pi_v<float>);
		sprite.setColor({((primaryAnmVm.color >> 16) & 0xff) / 255.0f,
		                 ((primaryAnmVm.color >> 8) & 0xff) / 255.0f,
		                 (primaryAnmVm.color & 0xff) / 255.0f,
		                 ((primaryAnmVm.color >> 24) & 0xff) / 255.0f});
		sprite.setBlendMode(primaryAnmVm.additive ? shiki::BlendMode::Add
		                                          : shiki::BlendMode::Alpha);
	} else if (!anmScript.empty()) {
		anmElapsedFrames += dt * 60.0f;
		const auto *frame = sampleTH06AnmFrame(anmScript, anmElapsedFrames);
		if (frame) {
			setSpriteIndex(frame->sprite);
			sprite.setScale(frame->flipX ? -1.0f : 1.0f, 1.0f);
		}
	} else if (!animSpriteIndices.empty()) {
		// Compatibility fallback when the original ANM file is unavailable.
		animFrameTimer += dt;
		if (animFrameTimer >= animFrameDuration) {
			animFrameTimer -= animFrameDuration;
			animFrameIndex = (animFrameIndex + 1) % animSpriteIndices.size();
			setSpriteIndex(animSpriteIndices[animFrameIndex]);
		}
	}

	for (auto &animation : auxiliaryAnimations) {
		if (!animation.active || !animation.vm.file)
			continue;
		animation.vm.tick();
		if (!animation.vm.visible && animation.vm.stopped) {
			animation.active = false;
			continue;
		}
		const auto tex =
		    resourceManager ? resourceManager->getSpriteTexture(
		                          animation.vm.file->atlas, animation.vm.sprite)
		                    : nullptr;
		if (!tex || !tex->isValid())
			continue;
		animation.sprite.setTexture(tex);
		const float width = static_cast<float>(tex->getWidth());
		const float height = static_cast<float>(tex->getHeight());
		animation.sprite.setSourceRect({0.0f, 0.0f, width, height});
		animation.sprite.setOrigin({width * 0.5f, height * 0.5f});
		animation.sprite.setScale(animation.vm.scaleX, animation.vm.scaleY);
		const float animationRotation =
		    animation.vm.autoRotate ? angle : animation.vm.rotation;
		animation.sprite.setRotation(-animationRotation * 180.0f /
		                             std::numbers::pi_v<float>);
		animation.sprite.setPosition(x + animation.vm.offsetX,
		                             y + animation.vm.offsetY);
		animation.sprite.setColor(
		    {((animation.vm.color >> 16) & 0xff) / 255.0f,
		     ((animation.vm.color >> 8) & 0xff) / 255.0f,
		     (animation.vm.color & 0xff) / 255.0f,
		     ((animation.vm.color >> 24) & 0xff) / 255.0f});
		animation.sprite.setBlendMode(animation.vm.additive
		                                  ? shiki::BlendMode::Add
		                                  : shiki::BlendMode::Alpha);
	}

	sprite.setPosition(x, y);
	if (rotateAnm)
		sprite.setRotation(-angle * 180.0f / std::numbers::pi_v<float>);
}

void setAuxiliaryAnimation(int slot, int scriptId) {
	if (slot < 0 || slot >= static_cast<int>(auxiliaryAnimations.size()))
		return;
	auto &animation = auxiliaryAnimations[static_cast<size_t>(slot)];
	animation = {};
	animation.scriptId = scriptId;
	const std::string primaryAtlas =
	    "stg" + std::to_string(stageNumber) + "enm";
	const std::string secondaryAtlas = primaryAtlas + "2";
	for (const auto &atlas : {primaryAtlas, secondaryAtlas}) {
		auto anm = std::make_shared<TH06MenuAnmFile>();
		if (!resourceManager ||
		    !anm->load(resourceManager->getAssetStore(), atlas) ||
		    !animation.vm.initialize(anm, scriptId))
			continue;
		animation.active = true;
		break;
	}
	if (animation.active)
		animation.vm.visible = animation.vm.sprite >= 0 && animation.vm.visible;
}

LaserState *createLaser(float laserX, float laserY, int color, float laserAngle,
                        float laserSpeed, float startOffset, float endOffset,
                        float startLength, float width, int startTime,
                        int duration, int despawnDuration, int hitboxStartTime,
                        int hitboxEndDelay, int flags) {
	auto found =
	    std::find_if(lasers.begin(), lasers.end(),
	                 [](const LaserState &laser) { return !laser.active; });
	if (found == lasers.end())
		return nullptr;
	const int laserIndex = static_cast<int>(found - lasers.begin());
	for (auto &reference : laserReferences) {
		if (reference == laserIndex)
			reference = -1;
	}
	auto &laser = *found;
	laser = {};
	laser.x = laserX;
	laser.y = laserY;
	laser.angle = laserAngle;
	laser.speed = laserSpeed;
	laser.startOffset = startOffset;
	laser.endOffset = endOffset;
	laser.startLength = startLength;
	laser.width = width;
	laser.startTime = startTime;
	laser.duration = duration;
	laser.despawnDuration = despawnDuration;
	laser.hitboxStartTime = hitboxStartTime;
	laser.hitboxEndDelay = hitboxEndDelay;
	laser.flags = flags;
	if (resourceManager) {
		auto laserTexture =
		    resourceManager->getSpriteTexture("etama3", 146 + color % 8);
		if (laserTexture && laserTexture->isValid()) {
			laser.sprite = shiki::Sprite(laserTexture);
			const float textureWidth =
			    static_cast<float>(laserTexture->getWidth());
			const float textureHeight =
			    static_cast<float>(laserTexture->getHeight());
			laser.sprite.setSourceRect(
			    {0.0f, 0.0f, textureWidth, textureHeight});
			laser.sprite.setOrigin({textureWidth * 0.5f, textureHeight * 0.5f});
			laser.sprite.setBlendMode(shiki::BlendMode::Add);
		}
		static constexpr std::array<int, 16> LASER_SPAWN_OFFSETS = {
		    0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 0};
		const int spawnOffset =
		    LASER_SPAWN_OFFSETS[static_cast<size_t>(color & 0xf)];
		auto flareTexture =
		    resourceManager->getSpriteTexture("etama3", 140 + spawnOffset);
		if (flareTexture && flareTexture->isValid()) {
			laser.flareSprite = shiki::Sprite(flareTexture);
			const float textureWidth =
			    static_cast<float>(flareTexture->getWidth());
			const float textureHeight =
			    static_cast<float>(flareTexture->getHeight());
			laser.flareSprite.setSourceRect(
			    {0.0f, 0.0f, textureWidth, textureHeight});
			laser.flareSprite.setOrigin(
			    {textureWidth * 0.5f, textureHeight * 0.5f});
			laser.flareSprite.setBlendMode(shiki::BlendMode::Add);
		}
	}
	laser.state = startTime == 0 ? 1 : 0;
	laser.active = true;
	return &laser;
}

void updateDirectionalAnimation() {
	if (anmPoseLeft < 0)
		return;
	int nextState = 0;
	if (vx < 0.0f)
		nextState = 1;
	else if (vx > 0.0f)
		nextState = 2;
	if (nextState == anmPoseState)
		return;

	int nextScript = anmPoseDefault;
	if (nextState == 1)
		nextScript = anmPoseLeft;
	else if (nextState == 2)
		nextScript = anmPoseRight;
	else if (anmPoseState == 1)
		nextScript = anmPoseFarLeft;
	else if (anmPoseState == 2)
		nextScript = anmPoseFarRight;
	anmPoseState = nextState;
	setAnimationScript(nextScript);
}

void spawnBulletPattern() {
	spawnBulletPatternAt(x + shootOffsetX, y + shootOffsetY,
	                     bulletPattern.angle1);
}

void spawnBulletPatternAt(float spawnX, float spawnY, float patternAngle) {
	if (!onSpawnBullet || !bulletPattern.configured)
		return;
	constexpr float PI = 3.14159265358979323846f;
	constexpr float TWO_PI = PI * 2.0f;
	const float aimAngle = std::atan2(playerY - spawnY, playerX - spawnX);
	auto randomRange = [](float from, float to) {
		const float unit =
		    static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
		return from + (to - from) * unit;
	};

	for (int layer = 0; layer < bulletPattern.count2; ++layer) {
		for (int index = 0; index < bulletPattern.count1; ++index) {
			float bulletAngle = 0.0f;
			float bulletSpeed = bulletPattern.speed1 -
			                    (bulletPattern.speed1 - bulletPattern.speed2) *
			                        static_cast<float>(layer) /
			                        static_cast<float>(bulletPattern.count2);

			switch (bulletPattern.aimMode) {
			case 0: // FAN_AIMED
			case 1: // FAN
				if ((bulletPattern.count1 & 1) != 0) {
					bulletAngle = static_cast<float>((index + 1) / 2) *
					              bulletPattern.angle2;
				} else {
					bulletAngle =
					    static_cast<float>(index / 2) * bulletPattern.angle2 +
					    bulletPattern.angle2 * 0.5f;
				}
				if ((index & 1) != 0)
					bulletAngle *= -1.0f;
				if (bulletPattern.aimMode == 0)
					bulletAngle += aimAngle;
				bulletAngle += patternAngle;
				break;
			case 2: // CIRCLE_AIMED
				bulletAngle += aimAngle;
				[[fallthrough]];
			case 3: // CIRCLE
				bulletAngle += static_cast<float>(index) * TWO_PI /
				               static_cast<float>(bulletPattern.count1);
				bulletAngle +=
				    static_cast<float>(layer) * bulletPattern.angle2 +
				    patternAngle;
				break;
			case 4: // OFFSET_CIRCLE_AIMED
				bulletAngle += aimAngle;
				[[fallthrough]];
			case 5: // OFFSET_CIRCLE
				bulletAngle += PI / static_cast<float>(bulletPattern.count1);
				bulletAngle += static_cast<float>(index) * TWO_PI /
				               static_cast<float>(bulletPattern.count1);
				bulletAngle += patternAngle;
				break;
			case 6: // RANDOM_ANGLE
				bulletAngle = randomRange(bulletPattern.angle2, patternAngle);
				break;
			case 7: // RANDOM_SPEED
				bulletSpeed =
				    randomRange(bulletPattern.speed2, bulletPattern.speed1);
				bulletAngle += static_cast<float>(index) * TWO_PI /
				               static_cast<float>(bulletPattern.count1);
				bulletAngle +=
				    static_cast<float>(layer) * bulletPattern.angle2 +
				    patternAngle;
				break;
			case 8: // RANDOM
				bulletAngle = randomRange(bulletPattern.angle2, patternAngle);
				bulletSpeed =
				    randomRange(bulletPattern.speed2, bulletPattern.speed1);
				break;
			default:
				break;
			}

			ECLBulletSpawn spawn;
			spawn.x = spawnX;
			spawn.y = spawnY;
			spawn.vx = bulletSpeed * 60.0f * std::cos(bulletAngle);
			spawn.vy = bulletSpeed * 60.0f * std::sin(bulletAngle);
			spawn.angle = bulletAngle;
			spawn.bulletType = bulletPattern.bulletType;
			spawn.bulletColor = bulletPattern.bulletColor;
			spawn.flags = bulletPattern.flags;
			spawn.exInts = bulletPattern.exInts;
			spawn.exFloats = bulletPattern.exFloats;
			onSpawnBullet(spawn);
			++spawnedBulletCount;
			if (isBoss && spawnedBulletCount == 1) {
				spdlog::info("Boss {} entered firing sub {} and spawned "
				             "its first bullet",
				             bossId, currentSubId);
			}
		}
	}
	if ((bulletPattern.flags & 0x200) != 0 && onSound)
		onSound(bulletPattern.soundId);
}

void runExInstruction(int exInstruction, int parameter = 0) {
	// Instructions 8/9/11/15 transform the global bullet pool and do not
	// use this enemy's shooter. Starbow starts EX 15 well before the boss
	// configures its first local pattern.
	const bool transformsBulletPool =
	    exInstruction == 8 || exInstruction == 9 || exInstruction == 11 ||
	    exInstruction == 15;
	const bool maintainsBatWings = exInstruction == 6 || exInstruction == 10;
	// EX instructions are valid before a local SHOOT opcode configures the
	// pattern; several original stage scripts initialize their variables or
	// provide a complete shooter payload inside the EX callback itself.

	const auto spawnBatWingEffect = [&]() {
		if (invisible || !onSpawnMovingEffect)
			return;

		batWingEffectAngle += std::numbers::pi_v<float> / 180.0f;
		if (batWingEffectAngle >= std::numbers::pi_v<float> / 4.0f)
			batWingEffectAngle -= std::numbers::pi_v<float> / 2.0f;

		const bool spawnThisFrame =
		    batWingEffectTimer > 120 ||
		    (batWingEffectTimer > 60 && batWingEffectTimer % 2 == 0) ||
		    (batWingEffectTimer > 30 && batWingEffectTimer % 4 == 0) ||
		    batWingEffectTimer % 8 == 0;
		if (spawnThisFrame) {
			const int phase = batWingEffectTimer % 16;
			const int halfPhase = phase / 2;
			const int distanceStep =
			    halfPhase > 0 ? std::rand() % halfPhase + halfPhase : 0;
			const float distance = distanceStep * 10.0f + 32.0f;
			const float angle =
			    batWingEffectAngle -
			    distanceStep * (std::numbers::pi_v<float> / 40.0f);
			const float verticalSpeed =
			    8.0f * static_cast<float>(distanceStep) / 60.0f - 4.0f / 15.0f;
			const auto spawnOne = [&](float effectX) {
				const float horizontalSpeed =
				    (static_cast<float>(std::rand()) /
				         static_cast<float>(RAND_MAX) * 40.0f -
				     20.0f) /
				    60.0f;
				onSpawnMovingEffect(19, effectX, y + std::sin(angle) * distance,
				                    0xff3030ff, horizontalSpeed, verticalSpeed,
				                    -horizontalSpeed / 120.0f,
				                    -verticalSpeed / 120.0f);
			};
			const float horizontalOffset = std::cos(angle) * distance;
			spawnOne(x + horizontalOffset);
			spawnOne(x - horizontalOffset);
		}
		++batWingEffectTimer;
	};

	switch (exInstruction) {
	case 0:
		if (onBulletTransform)
			onBulletTransform(exInstruction, parameter, x, y);
		break;

	case 1: { // ExInsShootAtRandomArea.
		const float rangeX = static_cast<float>(parameter);
		const float rangeY = rangeX * 0.75f;
		const float randomX =
		    static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
		const float randomY =
		    static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
		spawnBulletPatternAt(x + randomX * rangeX - rangeX * 0.5f,
		                     y + randomY * rangeY - rangeY * 0.5f,
		                     bulletPattern.angle1);
		break;
	}

	case 2: { // ExInsShootStarPattern.
		auto &frame = vars.getInt(-10003);
		const int duration = vars.getInt(-10004);
		if (frame >= duration) {
			repeatingExInstruction = -1;
			break;
		}
		if (frame == 0) {
			starPatternEnemyOrigin = {x, y};
			starPatternPlayerOrigin = {playerX, playerY};
			starPatternAngles[0] =
			    static_cast<float>(std::rand()) / RAND_MAX *
			    (2.0f * std::numbers::pi_v<float>)-std::numbers::pi_v<float>;
			starPatternAngles[1] = std::remainder(
			    starPatternAngles[0] + 4.0f * std::numbers::pi_v<float> / 5.0f,
			    2.0f * std::numbers::pi_v<float>);
		}
		if (frame % 30 == 0) {
			starPatternAngles[0] = starPatternAngles[1];
			for (size_t index = 1; index < starPatternAngles.size(); ++index)
				starPatternAngles[index] =
				    std::remainder(starPatternAngles[index - 1] +
				                       4.0f * std::numbers::pi_v<float> / 5.0f,
				                   2.0f * std::numbers::pi_v<float>);
		}
		if (frame % 6 == 0 && duration > 0) {
			float progress = static_cast<float>(frame) / duration;
			const float baseX =
			    std::lerp(starPatternEnemyOrigin.x, starPatternPlayerOrigin.x,
			              progress * 0.1f);
			const float baseY =
			    std::lerp(starPatternEnemyOrigin.y, starPatternPlayerOrigin.y,
			              progress * 0.1f);
			progress += 0.5f;
			const float interpolation = (frame % 30) / 30.0f;
			const float radius = vars.getFloat(-10008);
			const float savedSpeed = bulletPattern.speed1;
			for (size_t index = 0; index < 5; ++index) {
				const float ax = std::cos(starPatternAngles[index]) * radius;
				const float ay = std::sin(starPatternAngles[index]) * radius;
				const float bx =
				    std::cos(starPatternAngles[index + 1]) * radius;
				const float by =
				    std::sin(starPatternAngles[index + 1]) * radius;
				bulletPattern.speed1 =
				    savedSpeed + static_cast<float>(std::rand()) / RAND_MAX *
				                     bulletPattern.speed2;
				const float patternAngle =
				    std::numbers::pi_v<float> / 3.0f * progress -
				    static_cast<float>(index) * std::numbers::pi_v<float> /
				        6.0f * progress;
				spawnBulletPatternAt(baseX + std::lerp(ax, bx, interpolation),
				                     baseY + std::lerp(ay, by, interpolation),
				                     patternAngle);
			}
			bulletPattern.speed1 = savedSpeed;
			if (onSound)
				onSound(16);
		}
		++frame;
		break;
	}

	case 3: { // ExInsPatchouliShottypeSetVars.
		static constexpr int SHOT_VARS[2][2][3] = {{{0, 3, 1}, {2, 3, 4}},
		                                           {{1, 4, 0}, {4, 2, 3}}};
		const int character = std::clamp(playerCharacter, 0, 1);
		const int shotType = character == 0 ? 0 : 1;
		vars.getInt(-10002) = SHOT_VARS[character][shotType][0];
		vars.getInt(-10003) = SHOT_VARS[character][shotType][1];
		vars.getInt(-10004) = SHOT_VARS[character][shotType][2];
		break;
	}

	case 4: // ExInsStage56Func4.
		if (parameter < 2) {
			globalTimeStopped = parameter != 0;
			if (onSpawnEffect)
				onSpawnEffect(12, x, y, 1, 0xffffffff);
			if (onTimeStop)
				onTimeStop(parameter != 0);
		} else if (onBulletTransform) {
			onBulletTransform(exInstruction, parameter, x, y);
		}
		vars.getInt(-10003) = 0;
		break;

	case 5: { // ExInsStage5Func5.
		auto &frame = vars.getInt(-10003);
		if (frame % 9 == 0) {
			const int patternPosition = frame / 9;
			const float dx = playerX - x;
			const float dy = playerY - y;
			const float distance = std::max(std::hypot(dx, dy), 0.0001f);
			float tangentX = -dx / distance;
			float tangentY = -dy / distance;
			const float side = (patternPosition & 1) != 0 ? -256.0f : 256.0f;
			tangentX *= side;
			tangentY *= side;
			const float advance = 0.5f - patternPosition * 0.5f / 9.0f;
			const float offsetX = dx * advance + tangentX;
			const float offsetY = dy * advance + tangentY;
			float rayX = -tangentX;
			float rayY = -tangentY;
			const float initialCos = std::cos(std::numbers::pi_v<float> / 4.0f);
			const float initialSin = std::sin(std::numbers::pi_v<float> / 4.0f);
			const float initialX = rayX;
			rayX = initialX * initialCos + rayY * initialSin;
			rayY = -initialX * initialSin + rayY * initialCos;
			const float step = -std::numbers::pi_v<float> / 18.0f;
			const float stepCos = std::cos(step);
			const float stepSin = std::sin(step);
			const BulletPatternState saved = bulletPattern;
			bulletPattern.bulletType = 8;
			bulletPattern.bulletColor = 3;
			bulletPattern.aimMode = 0;
			bulletPattern.count1 = difficulty <= 1 ? 1 : 3;
			bulletPattern.count2 = 1;
			bulletPattern.speed1 = 2.0f;
			bulletPattern.speed2 = 2.0f;
			bulletPattern.angle2 = std::numbers::pi_v<float> / 6.0f;
			bulletPattern.flags = 0;
			bulletPattern.configured = true;
			float bulletAngle = -std::numbers::pi_v<float> / 4.0f;
			for (int index = 0; index < 9; ++index) {
				const float oldX = rayX;
				rayX = oldX * stepCos + rayY * stepSin;
				rayY = -oldX * stepSin + rayY * stepCos;
				const float angle =
				    (patternPosition & 1) != 0 && difficulty <= 1 ? bulletAngle
				                                                  : 0.0f;
				spawnBulletPatternAt(x + offsetX + rayX, y + offsetY + rayY,
				                     angle);
				bulletAngle += std::numbers::pi_v<float> / 18.0f;
			}
			bulletPattern = saved;
			if (onSound)
				onSound(7);
		}
		++frame;
		break;
	}

	case 6:  // ExInsBatWingEffect.
	case 10: // ExInsHandleBatTransformation also maintains the steam wings.
		spawnBatWingEffect();
		break;

	case 7: { // ExInsStage6Func7.
		const float randomAngle = static_cast<float>(std::rand()) / RAND_MAX *
		                          (2.0f * std::numbers::pi_v<float>);
		for (int group = 0; group < 2; ++group) {
			float laserAngle =
			    (group == 0 ? -std::numbers::pi_v<float>
			                : -7.0f * std::numbers::pi_v<float> / 8.0f) +
			    randomAngle;
			const float angleCorrection =
			    group == 0 ? std::numbers::pi_v<float> / 4.0f
			               : -std::numbers::pi_v<float> / 4.0f;
			std::array<shiki::Vec2, 8> positions{};
			for (auto &position : positions) {
				position = {x + std::cos(laserAngle) * 32.0f,
				            y + std::sin(laserAngle) * 32.0f};
				laserAngle += std::numbers::pi_v<float> / 4.0f;
			}
			laserAngle =
			    (group == 0 ? -std::numbers::pi_v<float>
			                : -7.0f * std::numbers::pi_v<float> / 8.0f) +
			    randomAngle;
			for (int layer = 0; layer < 3; ++layer) {
				const float length = layer < 2 ? 112.0f : 480.0f;
				for (auto &position : positions) {
					if (parameter == 0) {
						const bool lowerDifficulty = difficulty <= 1;
						createLaser(position.x, position.y,
						            lowerDifficulty ? 2 : 8, laserAngle, 0.0f,
						            0.0f, lowerDifficulty ? length : 440.0f,
						            lowerDifficulty ? length : 440.0f,
						            lowerDifficulty ? 28.0f : 20.0f,
						            layer * 16 + 60, 90 - layer * 16, 16, 50,
						            16, 2);
					} else {
						spawnBulletPatternAt(position.x, position.y,
						                     bulletPattern.angle1);
					}
					position.x += std::cos(laserAngle) * length;
					position.y += std::sin(laserAngle) * length;
					laserAngle += std::numbers::pi_v<float> / 4.0f;
				}
				laserAngle +=
				    angleCorrection - 2.0f * std::numbers::pi_v<float>;
			}
		}
		break;
	}

	case 8:  // Spawn a stationary ball at every active large bullet.
	case 9:  // Release stationary balls using boss-distance phase.
	case 11: // Release stationary balls in random directions.
	case 15: // Starbow/Catadioptric spatial bullet transformation.
		if (onBulletTransform)
			vars.getInt(-10004) =
			    onBulletTransform(exInstruction, parameter, x, y);
		if (exInstruction == 15)
			spawnBatWingEffect();
		break;

	case 13: { // ExInsStageXFunc13: Starbow Break's rotating emitters.
		const int patternCount = parameter;
		const float baseAngle = vars.getFloat(-10007);
		if (patternCount > 0 && vars.getInt(-10004) % 6 == 0) {
			for (int i = 0; i < patternCount; ++i) {
				const float angle =
				    baseAngle + static_cast<float>(i) *
				                    (2.0f * std::numbers::pi_v<float>) /
				                    static_cast<float>(patternCount);
				const float radius = vars.getFloat(-10008);
				spawnBulletPatternAt(192.0f + std::cos(angle) * radius,
				                     224.0f + std::sin(angle) * radius,
				                     angle + vars.getFloat(-10006));
			}
		}
		++vars.getInt(-10004);
		break;
	}

	case 12: // ExInsStage4Func12.
		for (const int laserIndex : laserReferences) {
			if (laserIndex < 0 || laserIndex >= static_cast<int>(lasers.size()))
				continue;
			const auto &laser = lasers[static_cast<size_t>(laserIndex)];
			if (laser.active)
				spawnBulletPatternAt(x + std::cos(laser.angle) * 64.0f,
				                     y + std::sin(laser.angle) * 64.0f,
				                     bulletPattern.angle1);
		}
		break;

	case 14: { // ExInsStageXFunc14: release bullets along active lasers.
		int activeLaserCount = 0;
		for (const int laserIndex : laserReferences) {
			if (laserIndex < 0 || laserIndex >= static_cast<int>(lasers.size()))
				continue;
			const auto &laser = lasers[static_cast<size_t>(laserIndex)];
			if (!laser.active)
				continue;
			++activeLaserCount;
			for (float offset = laser.startOffset; offset < laser.endOffset;
			     offset += 48.0f) {
				spawnBulletPatternAt(laser.x + std::cos(laser.angle) * offset,
				                     laser.y + std::sin(laser.angle) * offset,
				                     bulletPattern.angle1);
			}
		}
		vars.getInt(-10004) = activeLaserCount;
		break;
	}

	case 16: { // ExInsFlandreFinalContextUpdate.
		const int remainingLife = std::max(hp, 0);
		if (parameter == 0) {
			vars.getFloat(-10008) =
			    2.0f - static_cast<float>(remainingLife) / 6000.0f;
			// currentContext.var5 is ECL variable -10010.  Writing this
			// into a float slot made the final spell read a near-zero
			// firing delay.
			vars.getInt(-10010) = remainingLife * 240 / 6000 + 40;
		} else {
			const float xRange =
			    320.0f - static_cast<float>(remainingLife) * 160.0f / 6000.0f;
			const float yRange =
			    128.0f - static_cast<float>(remainingLife) * 64.0f / 6000.0f;
			const auto randomInRange = [](float range) {
				return static_cast<float>(std::rand()) /
				       static_cast<float>(RAND_MAX) * range;
			};
			vars.getFloat(-10007) =
			    randomInRange(xRange) + (192.0f - xRange * 0.5f);
			vars.getFloat(-10008) =
			    randomInRange(yRange) + (96.0f - yRange * 0.5f);
		}
		break;
	}

	default:
		break;
	}
}

void executeInstr(const shiki::ecl::ECLInstruction &instr) {
	auto resolveInt = [&](int32_t value) -> int32_t {
		if (value >= -10012 && value <= -10001) {
			return vars.getInt(value);
		}
		switch (value) {
		case -10013:
			return difficulty;
		case -10014:
			return rank;
		case -10015:
			return static_cast<int32_t>(x);
		case -10016:
			return static_cast<int32_t>(y);
		case -10017:
			return static_cast<int32_t>(z);
		case -10018:
			return static_cast<int32_t>(playerX);
		case -10019:
			return static_cast<int32_t>(playerY);
		case -10020:
			return 0;
		case -10021:
			return static_cast<int32_t>(std::atan2(playerY - y, playerX - x));
		case -10022:
			return bossTimerFrames;
		case -10023:
			return static_cast<int32_t>(std::hypot(playerX - x, playerY - y));
		case -10024:
			return hp;
		case -10025:
			return playerCharacter == 0 ? 0 : 3;
		default:
			return value;
		}
	};
	auto resolveFloatValue = [&](float value) -> float {
		const int32_t id = static_cast<int32_t>(value);
		if (value == static_cast<float>(id)) {
			if (id >= -10008 && id <= -10005)
				return vars.getFloat(id);
			if (id >= -10012 && id <= -10001)
				return static_cast<float>(vars.getInt(id));
			switch (id) {
			case -10013:
				return static_cast<float>(difficulty);
			case -10014:
				return static_cast<float>(rank);
			case -10015:
				return x;
			case -10016:
				return y;
			case -10017:
				return z;
			case -10018:
				return playerX;
			case -10019:
				return playerY;
			case -10020:
				return 0.0f;
			case -10021:
				return std::atan2(playerY - y, playerX - x);
			case -10022:
				return static_cast<float>(bossTimerFrames);
			case -10023:
				return std::hypot(playerX - x, playerY - y);
			case -10024:
				return static_cast<float>(hp);
			case -10025:
				return playerCharacter == 0 ? 0.0f : 3.0f;
			default:
				break;
			}
		}
		return value;
	};
	auto resolveF = [&](const auto &param) -> float {
		if (std::holds_alternative<float>(param.value)) {
			return resolveFloatValue(std::get<float>(param.value));
		}
		if (std::holds_alternative<int32_t>(param.value)) {
			return static_cast<float>(
			    resolveInt(std::get<int32_t>(param.value)));
		}
		return 0.0f;
	};
	auto setInt = [&](int32_t id, int32_t value) {
		if (id >= -10012 && id <= -10001)
			vars.getInt(id) = value;
		else if (id == -10024)
			hp = value;
	};
	auto setFloat = [&](int32_t id, float value) {
		if (id >= -10008 && id <= -10005)
			vars.getFloat(id) = value;
		else if (id == -10015)
			x = value;
		else if (id == -10016)
			y = value;
		else if (id == -10017)
			z = value;
	};
	auto jumpTo = [&](int32_t targetTime, int32_t relativeOffset) {
		if (!sub)
			return;
		const int64_t targetAddress =
		    static_cast<int64_t>(instr.address) + relativeOffset;
		for (size_t i = 0; i < sub->instructions.size(); ++i) {
			if (sub->instructions[i].address == targetAddress) {
				instrIndex = i;
				elapsed = static_cast<float>(targetTime) / 60.0f;
				return;
			}
		}
	};
	auto callSub = [&](int32_t calledSubId, int32_t intArg, float floatArg) {
		switchToSubroutine(calledSubId, true, intArg, floatArg, true);
	};
	switch (instr.id) {
	case 1: { // UNIMP terminates the enemy's ECL in TH06.
		alive = false;
		const bool bossEnemy =
		    isBoss || bossId >= 0 || subId == 16 || subId == 31;
		if (bossEnemy)
			notifyDeath(true);
		break;
	}
	case 2:
		if (instr.params.size() >= 2)
			jumpTo(std::get<int32_t>(instr.params[0].value),
			       std::get<int32_t>(instr.params[1].value));
		break;
	case 3:
		if (instr.params.size() >= 3) {
			const int32_t variable = std::get<int32_t>(instr.params[2].value);
			const int32_t next = resolveInt(variable) - 1;
			setInt(variable, next);
			if (next > 0)
				jumpTo(std::get<int32_t>(instr.params[0].value),
				       std::get<int32_t>(instr.params[1].value));
		}
		break;
	case 4:
		if (instr.params.size() >= 2) {
			const int32_t dst = std::get<int32_t>(instr.params[0].value);
			const int32_t src = std::get<int32_t>(instr.params[1].value);
			if (dst >= -10008 && dst <= -10005)
				setFloat(dst, resolveFloatValue(static_cast<float>(src)));
			else
				setInt(dst, resolveInt(src));
		}
		break;
	case 5:
		if (instr.params.size() >= 2)
			setFloat(std::get<int32_t>(instr.params[0].value),
			         resolveF(instr.params[1]));
		break;
	case 6:
		if (instr.params.size() >= 2) {
			const int32_t max =
			    static_cast<int32_t>(std::get<uint32_t>(instr.params[1].value));
			setInt(std::get<int32_t>(instr.params[0].value),
			       max > 0 ? std::rand() % max : 0);
		}
		break;
	case 7:
		if (instr.params.size() >= 3) {
			const int32_t range =
			    static_cast<int32_t>(std::get<uint32_t>(instr.params[1].value));
			setInt(std::get<int32_t>(instr.params[0].value),
			       resolveInt(std::get<int32_t>(instr.params[2].value)) +
			           (range > 0 ? std::rand() % range : 0));
		}
		break;
	case 8:
		if (instr.params.size() >= 2)
			setFloat(std::get<int32_t>(instr.params[0].value),
			         (static_cast<float>(std::rand()) / RAND_MAX) *
			             resolveF(instr.params[1]));
		break;
	case 9:
		if (instr.params.size() >= 3)
			setFloat(std::get<int32_t>(instr.params[0].value),
			         resolveF(instr.params[2]) +
			             (static_cast<float>(std::rand()) / RAND_MAX) *
			                 resolveF(instr.params[1]));
		break;
	case 10:
	case 11:
	case 12:
		if (!instr.params.empty())
			setFloat(std::get<int32_t>(instr.params[0].value),
			         instr.id == 10   ? x
			         : instr.id == 11 ? y
			                          : 0.0f);
		break;
	case 13:
	case 14:
	case 15:
	case 16:
	case 17:
		if (instr.params.size() >= 3) {
			const int32_t lhs =
			    resolveInt(std::get<int32_t>(instr.params[1].value));
			const int32_t rhs =
			    resolveInt(std::get<int32_t>(instr.params[2].value));
			int32_t result = 0;
			if (instr.id == 13)
				result = lhs + rhs;
			else if (instr.id == 14)
				result = lhs - rhs;
			else if (instr.id == 15)
				result = lhs * rhs;
			else if (instr.id == 16)
				result = rhs != 0 ? lhs / rhs : 0;
			else
				result = rhs != 0 ? lhs % rhs : 0;
			setInt(std::get<int32_t>(instr.params[0].value), result);
		}
		break;
	case 18:
	case 19:
		if (!instr.params.empty()) {
			const int32_t dst = std::get<int32_t>(instr.params[0].value);
			setInt(dst, resolveInt(dst) + (instr.id == 18 ? 1 : -1));
		}
		break;
	case 20:
	case 21:
	case 22:
	case 23:
	case 24:
		if (instr.params.size() >= 3) {
			const float lhs = resolveF(instr.params[1]);
			const float rhs = resolveF(instr.params[2]);
			float result = 0.0f;
			if (instr.id == 20)
				result = lhs + rhs;
			else if (instr.id == 21)
				result = lhs - rhs;
			else if (instr.id == 22)
				result = lhs * rhs;
			else if (instr.id == 23)
				result = rhs != 0.0f ? lhs / rhs : 0.0f;
			else
				result = rhs != 0.0f ? std::fmod(lhs, rhs) : 0.0f;
			setFloat(std::get<int32_t>(instr.params[0].value), result);
		}
		break;
	case 25:
		if (instr.params.size() >= 5)
			setFloat(
			    std::get<int32_t>(instr.params[0].value),
			    std::atan2(
			        resolveF(instr.params[4]) - resolveF(instr.params[2]),
			        resolveF(instr.params[3]) - resolveF(instr.params[1])));
		break;
	case 26:
		if (!instr.params.empty()) {
			const int32_t dst = std::get<int32_t>(instr.params[0].value);
			setFloat(dst,
			         std::remainder(resolveFloatValue(static_cast<float>(dst)),
			                        2.0f * 3.14159265f));
		}
		break;
	case 27:
		if (instr.params.size() >= 2) {
			const int32_t lhs =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			const int32_t rhs =
			    resolveInt(std::get<int32_t>(instr.params[1].value));
			compareRegister = (lhs > rhs) - (lhs < rhs);
		}
		break;
	case 28:
		if (instr.params.size() >= 2) {
			const float lhs = resolveF(instr.params[0]);
			const float rhs = resolveF(instr.params[1]);
			compareRegister = (lhs > rhs) - (lhs < rhs);
		}
		break;
	case 29:
	case 30:
	case 31:
	case 32:
	case 33:
	case 34:
		if (instr.params.size() >= 2) {
			const bool take = instr.id == 29   ? compareRegister < 0
			                  : instr.id == 30 ? compareRegister <= 0
			                  : instr.id == 31 ? compareRegister == 0
			                  : instr.id == 32 ? compareRegister > 0
			                  : instr.id == 33 ? compareRegister >= 0
			                                   : compareRegister != 0;
			if (take)
				jumpTo(std::get<int32_t>(instr.params[0].value),
				       std::get<int32_t>(instr.params[1].value));
		}
		break;
	case 35:
		if (instr.params.size() >= 3)
			callSub(std::get<int32_t>(instr.params[0].value),
			        resolveInt(std::get<int32_t>(instr.params[1].value)),
			        resolveF(instr.params[2]));
		break;
	case 36:
		if (!callStack.empty()) {
			auto saved = callStack.back();
			callStack.pop_back();
			sub = saved.savedSub;
			instrIndex = saved.savedInstrIndex;
			elapsed = saved.savedElapsed;
			waitTimer = saved.savedWaitTimer;
			vars = saved.savedVars;
			compareRegister = saved.savedCompareRegister;
			repeatingExInstruction = saved.savedRepeatingExInstruction;
		}
		break;
	case 37:
	case 38:
	case 39:
	case 40:
	case 41:
	case 42:
		if (instr.params.size() >= 5) {
			const int32_t lhs =
			    resolveInt(std::get<int32_t>(instr.params[3].value));
			const int32_t rhs =
			    resolveInt(std::get<int32_t>(instr.params[4].value));
			const bool take = instr.id == 37   ? lhs < rhs
			                  : instr.id == 38 ? lhs <= rhs
			                  : instr.id == 39 ? lhs == rhs
			                  : instr.id == 40 ? lhs > rhs
			                  : instr.id == 41 ? lhs >= rhs
			                                   : lhs != rhs;
			if (take)
				callSub(std::get<int32_t>(instr.params[0].value),
				        resolveInt(std::get<int32_t>(instr.params[1].value)),
				        resolveF(instr.params[2]));
		}
		break;

	case 43: // MOVEPOSITION — instant set position (fff)
		if (instr.params.size() >= 3) {
			x = resolveF(instr.params[0]);
			y = resolveF(instr.params[1]);
			vx = 0;
			vy = 0;
			movementMode = 0;
		}
		break;

	case 44: // MOVEAXISVELOCITY — set axis velocity (fff)
		if (instr.params.size() >= 3) {
			vx = resolveF(instr.params[0]);
			vy = resolveF(instr.params[1]);
			movementMode = 0;
		}
		break;

	case 45: // MOVEVELOCITY — set angle (rad) + speed
		if (instr.params.size() >= 2) {
			angle = resolveF(instr.params[0]);
			speed = resolveF(instr.params[1]);
			movementMode = 1;
		}
		break;

	case 46: // MOVEANGULARVELOCITY — set angular velocity (rad/frame)
		if (instr.params.size() >= 1) {
			angularVelocity = resolveF(instr.params[0]);
			movementMode = 1;
		}
		break;

	case 47: // MOVESPEED — set speed (px/frame)
		if (instr.params.size() >= 1) {
			speed = resolveF(instr.params[0]);
			movementMode = 1;
		}
		break;

	case 48: // MOVEACCELERATION — set linear acceleration (px/frame²)
		if (instr.params.size() >= 1) {
			linearAccel = resolveF(instr.params[0]);
			movementMode = 1;
		}
		break;

	case 49: // MOVERAND — random angle in range [min, max]
		if (instr.params.size() >= 2) {
			float minA = resolveF(instr.params[0]);
			float maxA = resolveF(instr.params[1]);
			float r =
			    static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
			angle = minA + r * (maxA - minA);
			movementMode = 1;
		}
		break;

	case 50: // MOVERANDINBOUND — random angle + boundary reflection
		if (instr.params.size() >= 2) {
			float minA = resolveF(instr.params[0]);
			float maxA = resolveF(instr.params[1]);
			float r =
			    static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
			angle = minA + r * (maxA - minA);
			movementMode = 1;
			// reflect angle near screen edges (TH06 coords)
			if (x < boundMinX + 96.0f) {
				if (angle > 1.5707963f)
					angle = 3.14159265f - angle;
				else if (angle < -1.5707963f)
					angle = -3.14159265f - angle;
			}
			if (x > boundMaxX - 96.0f) {
				if (angle < 1.5707963f && angle >= 0.0f)
					angle = 3.14159265f - angle;
				else if (angle > -1.5707963f && angle <= 0.0f)
					angle = -3.14159265f - angle;
			}
			if (y < boundMinY + 48.0f && angle < 0.0f)
				angle = -angle;
			if (y > boundMaxY - 48.0f && angle > 0.0f)
				angle = -angle;
		}
		break;

	case 51: // MOVEATPLAYER — aim at player + speed
		if (instr.params.size() >= 2) {
			float offsetAngle = resolveF(instr.params[0]);
			speed = resolveF(instr.params[1]);
			angle = std::atan2(playerY - y, playerX - x) + offsetAngle;
			movementMode = 1;
		}
		break;

	case 52: // MoveDirTime (decelerate)
	case 53:
	case 54:
	case 55:
		if (instr.params.size() >= 3) {
			float duration =
			    static_cast<float>(std::get<int32_t>(instr.params[0].value));
			float dirAngle = resolveF(instr.params[1]);
			float dirSpeed = resolveF(instr.params[2]);
			moveInterpX = std::cos(dirAngle) * dirSpeed * duration / 2.0f;
			moveInterpY = std::sin(dirAngle) * dirSpeed * duration / 2.0f;
			moveInterpStartX = x;
			moveInterpStartY = y;
			moveInterpDuration = duration;
			moveInterpTimer = duration;
			movementEaseType = instr.id - 52 + 1;
			movementMode = 2;
		}
		break;

	case 56: // MovePosTime (linear)
	case 57:
	case 58:
	case 59:
	case 60:
		if (instr.params.size() >= 3) {
			float duration =
			    static_cast<float>(std::get<int32_t>(instr.params[0].value));
			float tx = resolveF(instr.params[1]);
			float ty = resolveF(instr.params[2]);
			moveInterpX = tx - x;
			moveInterpY = ty - y;
			moveInterpStartX = x;
			moveInterpStartY = y;
			moveInterpDuration = duration;
			moveInterpTimer = duration;
			movementEaseType = instr.id - 56;
			movementMode = 2;
		}
		break;

	case 61:
	case 62:
	case 63:
	case 64:
		if (instr.params.size() >= 1) {
			int32_t frames = std::get<int32_t>(instr.params[0].value);
			float duration = static_cast<float>(frames);
			moveInterpX = std::cos(angle) * speed * duration / 2.0f;
			moveInterpY = std::sin(angle) * speed * duration / 2.0f;
			moveInterpStartX = x;
			moveInterpStartY = y;
			moveInterpDuration = duration;
			moveInterpTimer = duration;
			movementEaseType = instr.id - 61 + 1; // 61→1, 62→2, 63→3, 64→4
			movementMode = 2;
		}
		break;

	case 65: // Set boundary (stored, enforced every frame in update)
		if (instr.params.size() >= 4) {
			boundMinX = resolveF(instr.params[0]);
			boundMinY = resolveF(instr.params[1]);
			boundMaxX = resolveF(instr.params[2]);
			boundMaxY = resolveF(instr.params[3]);
			shouldClampPos = true;
			// immediate clamp
			if (x < boundMinX)
				x = boundMinX;
			if (x > boundMaxX)
				x = boundMaxX;
			if (y < boundMinY)
				y = boundMinY;
			if (y > boundMaxY)
				y = boundMaxY;
		}
		break;

	case 66: // MOVEBOUNDSDISABLE
		shouldClampPos = false;
		break;

	case 67:
	case 68:
	case 69:
	case 70:
	case 71:
	case 72:
	case 73:
	case 74:
	case 75:
		if (instr.params.size() >= 9) {
			constexpr int TH06_EXTRA_START_RANK = 16;
			const auto rankAmount = [](int low, int high) {
				return TH06_EXTRA_START_RANK * (high - low) / 32 + low;
			};
			const auto rankSpeed = [](float low, float high) {
				return static_cast<float>(TH06_EXTRA_START_RANK) *
				           (high - low) / 32.0f +
				       low;
			};
			bulletPattern.bulletType =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			bulletPattern.bulletColor =
			    resolveInt(std::get<int32_t>(instr.params[1].value));
			bulletPattern.count1 = std::max(
			    resolveInt(std::get<int32_t>(instr.params[2].value)) +
			        rankAmount(bulletRankAmount1Low, bulletRankAmount1High),
			    1);
			bulletPattern.count2 = std::max(
			    resolveInt(std::get<int32_t>(instr.params[3].value)) +
			        rankAmount(bulletRankAmount2Low, bulletRankAmount2High),
			    1);
			bulletPattern.speed1 = resolveF(instr.params[4]);
			bulletPattern.speed2 = resolveF(instr.params[5]);
			const bool stationaryPattern =
			    bulletPattern.speed1 == 0.0f && bulletPattern.speed2 == 0.0f;
			const float speedRank =
			    rankSpeed(bulletRankSpeedLow, bulletRankSpeedHigh);
			if (bulletPattern.speed1 != 0.0f)
				bulletPattern.speed1 =
				    std::max(bulletPattern.speed1 + speedRank, 0.3f);
			if (!stationaryPattern)
				bulletPattern.speed2 =
				    std::max(bulletPattern.speed2 + speedRank * 0.5f, 0.3f);
			bulletPattern.angle1 = resolveF(instr.params[6]);
			bulletPattern.angle2 = resolveF(instr.params[7]);
			bulletPattern.flags =
			    resolveInt(std::get<int32_t>(instr.params[8].value));
			bulletPattern.aimMode = static_cast<int>(instr.id) - 67;
			bulletPattern.configured = true;
			if (inFiring)
				spawnBulletPattern();
		}
		break;

	case 76: // SHOOTINTERVAL — set shoot interval (frames)
		if (instr.params.size() >= 1) {
			int32_t frames =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			shootInterval = static_cast<float>(frames) / 60.0f;
			shootTimer = 0.0f;
		}
		break;

	case 77: // SHOOTINTERVALDELAYED — set shoot interval with random
	         // initial delay
		if (instr.params.size() >= 1) {
			int32_t frames =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			shootInterval = static_cast<float>(frames) / 60.0f;
			shootTimer = frames > 0
			                 ? static_cast<float>(std::rand() % frames) / 60.0f
			                 : 0.0f;
		}
		break;

	case 78: // SHOOTDISABLED
		inFiring = false;
		break;

	case 79: // SHOOTENABLED
		inFiring = true;
		break;

	case 80: // SHOOTNOW
		spawnBulletPattern();
		break;

	case 81: // SHOOTOFFSET — set bullet spawn offset
		if (instr.params.size() >= 3) {
			shootOffsetX = resolveF(instr.params[0]);
			shootOffsetY = resolveF(instr.params[1]);
		}
		break;

	case 82: // BULLETEFFECTS
		if (instr.params.size() >= 8) {
			for (size_t i = 0; i < 4; ++i)
				bulletPattern.exInts[i] =
				    resolveInt(std::get<int32_t>(instr.params[i].value));
			for (size_t i = 0; i < 4; ++i)
				bulletPattern.exFloats[i] = resolveF(instr.params[i + 4]);
		}
		break;

	case 83: // BULLETCANCEL
		if (onBulletCancel)
			onBulletCancel();
		break;

	case 84: // BULLETSOUND
		if (!instr.params.empty()) {
			bulletPattern.soundId =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			if (bulletPattern.soundId >= 0)
				bulletPattern.flags |= 0x200;
			else
				bulletPattern.flags &= ~0x200;
		}
		break;

	case 85: // LASERCREATE
	case 86: // LASERCREATEAIMED
		if (instr.params.size() >= 14 && laserStore >= 0 &&
		    laserStore < static_cast<int>(laserReferences.size())) {
			const int color =
			    resolveInt(std::get<int32_t>(instr.params[1].value));
			float laserAngle = resolveF(instr.params[2]);
			if (instr.id == 86)
				laserAngle += std::atan2(playerY - y, playerX - x);
			auto *laser = createLaser(
			    x + shootOffsetX, y + shootOffsetY, color, laserAngle,
			    resolveF(instr.params[3]), resolveF(instr.params[4]),
			    resolveF(instr.params[5]), resolveF(instr.params[6]),
			    resolveF(instr.params[7]),
			    resolveInt(std::get<int32_t>(instr.params[8].value)),
			    resolveInt(std::get<int32_t>(instr.params[9].value)),
			    resolveInt(std::get<int32_t>(instr.params[10].value)),
			    resolveInt(std::get<int32_t>(instr.params[11].value)),
			    resolveInt(std::get<int32_t>(instr.params[12].value)),
			    resolveInt(std::get<int32_t>(instr.params[13].value)));
			laserReferences[static_cast<size_t>(laserStore)] =
			    laser ? static_cast<int>(laser - lasers.data()) : -1;
		}
		break;

	case 87: // LASERINDEX
		if (!instr.params.empty())
			laserStore = resolveInt(std::get<int32_t>(instr.params[0].value));
		break;

	case 88: // LASERROTATE
		if (instr.params.size() >= 2) {
			const int index =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			const int laserIndex =
			    index >= 0 && index < static_cast<int>(laserReferences.size())
			        ? laserReferences[static_cast<size_t>(index)]
			        : -1;
			if (laserIndex >= 0 &&
			    laserIndex < static_cast<int>(lasers.size()) &&
			    lasers[static_cast<size_t>(laserIndex)].active)
				lasers[static_cast<size_t>(laserIndex)].angle +=
				    resolveF(instr.params[1]);
		}
		break;

	case 89: // LASERROTATEFROMPLAYER
		if (instr.params.size() >= 2) {
			const int index =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			const int laserIndex =
			    index >= 0 && index < static_cast<int>(laserReferences.size())
			        ? laserReferences[static_cast<size_t>(index)]
			        : -1;
			if (laserIndex >= 0 &&
			    laserIndex < static_cast<int>(lasers.size()) &&
			    lasers[static_cast<size_t>(laserIndex)].active) {
				auto &laser = lasers[static_cast<size_t>(laserIndex)];
				laser.angle = std::atan2(playerY - laser.y, playerX - laser.x) +
				              resolveF(instr.params[1]);
			}
		}
		break;

	case 90: // LASEROFFSET
		if (instr.params.size() >= 4) {
			const int index =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			const int laserIndex =
			    index >= 0 && index < static_cast<int>(laserReferences.size())
			        ? laserReferences[static_cast<size_t>(index)]
			        : -1;
			if (laserIndex >= 0 &&
			    laserIndex < static_cast<int>(lasers.size()) &&
			    lasers[static_cast<size_t>(laserIndex)].active) {
				auto &laser = lasers[static_cast<size_t>(laserIndex)];
				laser.x = x + resolveF(instr.params[1]);
				laser.y = y + resolveF(instr.params[2]);
			}
		}
		break;

	case 91: // LASERTEST
		if (!instr.params.empty()) {
			const int index =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			const int laserIndex =
			    index >= 0 && index < static_cast<int>(laserReferences.size())
			        ? laserReferences[static_cast<size_t>(index)]
			        : -1;
			compareRegister =
			    laserIndex >= 0 &&
			            laserIndex < static_cast<int>(lasers.size()) &&
			            lasers[static_cast<size_t>(laserIndex)].active
			        ? 0
			        : 1;
		}
		break;

	case 92: // LASERCANCEL
		if (!instr.params.empty()) {
			const int index =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			const int laserIndex =
			    index >= 0 && index < static_cast<int>(laserReferences.size())
			        ? laserReferences[static_cast<size_t>(index)]
			        : -1;
			if (laserIndex >= 0 &&
			    laserIndex < static_cast<int>(lasers.size())) {
				auto &laser = lasers[static_cast<size_t>(laserIndex)];
				if (laser.active && laser.state < 2) {
					laser.state = 2;
					laser.stateTimer = 0;
					laser.cancelFrames = 0;
				}
			}
		}
		break;

	case 93: // SPELLCARDSTART
		spellActive = true;
		bulletRankSpeedLow = -0.5f;
		bulletRankSpeedHigh = 0.5f;
		bulletRankAmount1Low = 0;
		bulletRankAmount1High = 0;
		bulletRankAmount2Low = 0;
		bulletRankAmount2High = 0;
		if (instr.params.size() >= 3 && onSpellStart) {
			std::string name = std::get<std::string>(instr.params[2].value);
			if (const auto nul = name.find('\0'); nul != std::string::npos)
				name.resize(nul);
			onSpellStart(std::get<int32_t>(instr.params[1].value),
			             std::get<int32_t>(instr.params[0].value), name);
		}
		break;

	case 94: // SPELLCARDEND
	{
		const bool wasSpellActive =
		    isGlobalSpellActive ? isGlobalSpellActive() : spellActive;
		spellActive = false;
		if (wasSpellActive && onSpellEnd) {
			const int secondsRemaining =
			    timerCallbackThreshold >= 0
			        ? std::max(0, timerCallbackThreshold - bossTimerFrames) / 60
			        : 0;
			onSpellEnd(secondsRemaining);
		}
		break;
	}

	case 95: // Create enemy with param
		if (instr.params.size() >= 7) {
			int32_t esub = std::get<int32_t>(instr.params[0].value);
			float eposX = resolveF(instr.params[1]);
			float eposY = resolveF(instr.params[2]);
			float eposZ = resolveF(instr.params[3]);
			int32_t life = std::get<int32_t>(instr.params[4].value);
			if (onSpawnChildEnemy)
				onSpawnChildEnemy(0, esub, eposX, eposY, eposZ, 0.0f, 0.0f,
				                  0.0f, 0.0f, life);
		}
		break;

	case 96: // ENEMYKILLALL
		if (onKillAllEnemies)
			onKillAllEnemies();
		break;

	case 97: // ANMSETMAIN
		if (instr.params.size() >= 1)
			setAnimationScript(std::get<int32_t>(instr.params[0].value));
		break;

	case 98: // ANMSETPOSES
		if (instr.params.size() >= 5) {
			anmPoseDefault =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			anmPoseFarLeft =
			    resolveInt(std::get<int32_t>(instr.params[1].value));
			anmPoseFarRight =
			    resolveInt(std::get<int32_t>(instr.params[2].value));
			anmPoseLeft = resolveInt(std::get<int32_t>(instr.params[3].value));
			anmPoseRight = resolveInt(std::get<int32_t>(instr.params[4].value));
			anmPoseState = 0xff;
		}
		break;

	case 99: // ANMSETSLOT
		if (instr.params.size() >= 2)
			setAuxiliaryAnimation(
			    resolveInt(std::get<int32_t>(instr.params[0].value)),
			    resolveInt(std::get<int32_t>(instr.params[1].value)));
		break;

	case 100: // ANMSETDEATH
		if (!instr.params.empty()) {
			const uint32_t packed = static_cast<uint32_t>(
			    resolveInt(std::get<int32_t>(instr.params[0].value)));
			deathAnm1 = static_cast<int>(packed & 0xff);
			deathAnm2 = static_cast<int>((packed >> 8) & 0xff);
			deathAnm3 = static_cast<int>((packed >> 16) & 0xff);
		}
		break;

	case 101: // BOSSSET
		if (!instr.params.empty()) {
			const int32_t bossId =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			this->bossId = bossId;
			isBoss = this->bossId >= 0;
			if (onBossChanged)
				onBossChanged(isBoss);
		}
		break;

	case 102: // SPELLCARDEFFECT
		if (instr.params.size() >= 5 &&
		    spellEffectCount < spellEffects.size()) {
			auto &effect = spellEffects[spellEffectCount++];
			effect.colorId =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			effect.axisX = resolveF(instr.params[1]);
			effect.axisY = resolveF(instr.params[2]);
			effect.axisZ = resolveF(instr.params[3]);
			effect.targetDistance = resolveF(instr.params[4]);
		}
		break;

	case 103: // Set collision box
		if (instr.params.size() >= 2) {
			hitboxWidth = std::abs(resolveF(instr.params[0]));
			hitboxHeight = std::abs(resolveF(instr.params[1]));
		}
		break;

	case 104: // ENEMYFLAGCOLLISION
		if (!instr.params.empty())
			collidable =
			    resolveInt(std::get<int32_t>(instr.params[0].value)) != 0;
		break;

	case 105: // ENEMYFLAGCANTAKEDAMAGE
		if (!instr.params.empty())
			damageable =
			    resolveInt(std::get<int32_t>(instr.params[0].value)) != 0;
		break;

	case 106: // EFFECTSOUND
		if (!instr.params.empty() && onSound)
			onSound(resolveInt(std::get<int32_t>(instr.params[0].value)));
		break;

	case 107: // ENEMYFLAGDEATH
		if (!instr.params.empty())
			deathMode = resolveInt(std::get<int32_t>(instr.params[0].value));
		break;

	case 108: // DEATHCALLBACKSUB
		if (!instr.params.empty())
			deathCallbackSub = std::get<int32_t>(instr.params[0].value);
		break;
	case 109: // ENEMYINTERRUPTSET
		if (instr.params.size() >= 2)
			interrupts[std::get<int32_t>(instr.params[1].value)] =
			    std::get<int32_t>(instr.params[0].value);
		break;
	case 110: // ENEMYINTERRUPT
		if (!instr.params.empty()) {
			const int interruptId =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			triggerInterrupt(interruptId);
		}
		break;

	case 111: // Enemy life
		if (instr.params.size() >= 1) {
			hp = maxHp = std::get<int32_t>(instr.params[0].value);
			deathResolved = false;
		}
		break;

	case 112: // BOSSTIMERSET
		if (!instr.params.empty())
			bossTimerFrames =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
		break;
	case 113: // LIFECALLBACKTHRESHOLD
		if (!instr.params.empty())
			lifeCallbackThreshold =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
		break;

	case 114: // LIFECALLBACKSUB
		if (!instr.params.empty())
			lifeCallbackSub = std::get<int32_t>(instr.params[0].value);
		break;
	case 115: // TIMERCALLBACKTHRESHOLD
		if (!instr.params.empty()) {
			timerCallbackThreshold =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
		}
		break;
	case 116: // TIMERCALLBACKSUB
		if (!instr.params.empty())
			timerCallbackSub = std::get<int32_t>(instr.params[0].value);
		break;

	case 117: // ENEMYFLAGINTERACTABLE
		if (!instr.params.empty())
			interactable =
			    resolveInt(std::get<int32_t>(instr.params[0].value)) != 0;
		break;

	case 118: // EFFECTPARTICLE
		if (instr.params.size() >= 3 && onSpawnEffect) {
			const int effectId =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			const int count =
			    std::holds_alternative<uint32_t>(instr.params[1].value)
			        ? static_cast<int>(
			              std::get<uint32_t>(instr.params[1].value))
			        : resolveInt(std::get<int32_t>(instr.params[1].value));
			const uint32_t color =
			    std::holds_alternative<uint32_t>(instr.params[2].value)
			        ? std::get<uint32_t>(instr.params[2].value)
			        : static_cast<uint32_t>(
			              resolveInt(std::get<int32_t>(instr.params[2].value)));
			onSpawnEffect(effectId, x, y, count, color);
		}
		break;

	case 119: // DROPITEMS
		if (!instr.params.empty() && onDropItem) {
			const int count =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			for (int index = 0; index < count; ++index) {
				const float offsetX =
				    static_cast<float>(std::rand() % 145) - 72.0f;
				const float offsetY =
				    static_cast<float>(std::rand() % 145) - 72.0f;
				onDropItem(TH06_ITEM_RANDOM, x + offsetX, y + offsetY, 0);
			}
		}
		break;

	case 120: // ANMFLAGROTATION
		if (!instr.params.empty())
			rotateAnm =
			    resolveInt(std::get<int32_t>(instr.params[0].value)) != 0;
		break;

	case 124: // DROPITEMID
		if (!instr.params.empty())
			itemDrop = resolveInt(std::get<int32_t>(instr.params[0].value));
		break;

	case 121: // EXINSCALL
		if (instr.params.size() >= 2) {
			const int exInstruction =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			runExInstruction(exInstruction, resolveInt(std::get<int32_t>(
			                                    instr.params[1].value)));
		}
		break;

	case 122: // EXINSREPEAT
		if (!instr.params.empty())
			repeatingExInstruction =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
		break;

	case 123: // TIMESET
		if (!instr.params.empty())
			elapsed += static_cast<float>(resolveInt(
			               std::get<int32_t>(instr.params[0].value))) /
			           60.0f;
		break;

	case 126: // BOSSSETLIFECOUNT
		if (!instr.params.empty())
			bossLifeCount =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
		break;

	case 128: // ANMINTERRUPTMAIN
		if (!instr.params.empty() && primaryAnmVm.file)
			primaryAnmVm.interrupt(
			    resolveInt(std::get<int32_t>(instr.params[0].value)));
		break;

	case 129: // ANMINTERRUPTSLOT
		if (instr.params.size() >= 2) {
			const int slot =
			    resolveInt(std::get<int32_t>(instr.params[0].value));
			if (slot >= 0 &&
			    slot < static_cast<int>(auxiliaryAnimations.size()))
				auxiliaryAnimations[static_cast<size_t>(slot)].vm.interrupt(
				    resolveInt(std::get<int32_t>(instr.params[1].value)));
		}
		break;

	case 130: // ENEMYFLAGDISABLECALLSTACK
		if (!instr.params.empty())
			disableCallStack =
			    resolveInt(std::get<int32_t>(instr.params[0].value)) != 0;
		break;

	case 135: // SPELLCARDFLAGTIMEOUT
		if (!instr.params.empty())
			timeoutSpell =
			    resolveInt(std::get<int32_t>(instr.params[0].value)) != 0;
		break;

	case 131: // BULLETRANKINFLUENCE
		if (instr.params.size() >= 6) {
			bulletRankSpeedLow = resolveF(instr.params[0]);
			bulletRankSpeedHigh = resolveF(instr.params[1]);
			bulletRankAmount1Low =
			    resolveInt(std::get<int32_t>(instr.params[2].value));
			bulletRankAmount1High =
			    resolveInt(std::get<int32_t>(instr.params[3].value));
			bulletRankAmount2Low =
			    resolveInt(std::get<int32_t>(instr.params[4].value));
			bulletRankAmount2High =
			    resolveInt(std::get<int32_t>(instr.params[5].value));
		}
		break;

	case 133: // BOSSTIMERCLEAR
		timerCallbackSub = deathCallbackSub;
		bossTimerFrames = 0;
		break;

	case 134: // LASERCLEARALL
		laserReferences.fill(-1);
		break;

	case 132: // ENEMYFLAGINVISIBLE
		if (!instr.params.empty())
			invisible =
			    resolveInt(std::get<int32_t>(instr.params[0].value)) != 0;
		break;

	default:
		spdlog::debug("ECLEnemy unhandled sub instr: id={}", instr.id);
		break;
	}
}

void setAnimationScript(int32_t scriptId) {
	animationScript = scriptId;
	primaryAnmVm = {};
	animSpriteIndices.clear();
	animFrameIndex = 0;
	animFrameTimer = 0.0f;
	animFrameDuration = 1.0f / 60.0f;
	anmScript = {};
	anmElapsedFrames = 0.0f;
	struct ScriptSprite {
		int script;
		int sprite;
	};
	static constexpr std::array<ScriptSprite, 26> STG7_SCRIPTS = {{
	    {0, 0},   {1, 4},   {2, 8},   {3, 12},  {4, 16},  {5, 20},  {6, 16},
	    {7, 20},  {8, 24},  {9, 24},  {10, 25}, {11, 29}, {12, 33}, {13, 43},
	    {14, 44}, {15, 45}, {16, 46}, {17, 46}, {18, 46}, {64, 37}, {65, 38},
	    {66, 39}, {67, 38}, {68, 38}, {69, 40}, {70, 40},
	}};
	static constexpr std::array<ScriptSprite, 6> STG7_BOSS_SCRIPTS = {{
	    {160, 0},
	    {161, 0},
	    {162, 0},
	    {163, 5},
	    {164, 5},
	    {165, 8},
	}};
	const std::string primaryAtlas =
	    "stg" + std::to_string(stageNumber) + "enm";
	const std::string secondaryAtlas = primaryAtlas + "2";
	auto *assetStore = resourceManager ? const_cast<shiki::asset::AssetStore *>(
	                                         resourceManager->getAssetStore())
	                                   : nullptr;
	for (const auto &candidate : {primaryAtlas, secondaryAtlas}) {
		auto source = std::make_shared<TH06MenuAnmFile>();
		if (resourceManager &&
		    source->load(resourceManager->getAssetStore(), candidate) &&
		    primaryAnmVm.initialize(source, scriptId)) {
			atlasName = candidate;
			setSpriteIndex(primaryAnmVm.sprite);
			return;
		}
		anmScript = loadTH06AnmScript(assetStore, candidate, scriptId);
		if (!anmScript.empty()) {
			atlasName = candidate;
			setSpriteIndex(anmScript.frames.front().sprite);
			return;
		}
	}

	auto apply = [&](const auto &scripts, const char *atlas) {
		const auto found = std::find_if(
		    scripts.begin(), scripts.end(),
		    [scriptId](const auto &entry) { return entry.script == scriptId; });
		if (found == scripts.end())
			return false;
		if (!resourceManager)
			return true;
		auto tex = resourceManager->getSpriteTexture(atlas, found->sprite);
		if (!tex || !tex->isValid())
			return false;
		atlasName = atlas;
		texture = tex;
		sprite.setTexture(tex);
		const float width = static_cast<float>(tex->getWidth());
		const float height = static_cast<float>(tex->getHeight());
		sprite.setSourceRect({0.0f, 0.0f, width, height});
		sprite.setOrigin({width * 0.5f, height * 0.5f});
		sprite.setScale(1.0f, 1.0f);
		return true;
	};

	const bool loaded = stageNumber == 7 &&
	                    (scriptId >= 160 ? apply(STG7_BOSS_SCRIPTS, "stg7enm2")
	                                     : apply(STG7_SCRIPTS, "stg7enm"));
	if (!loaded)
		spdlog::warn("Unknown or unavailable TH06 stage {} ANM script: {}",
		             stageNumber, scriptId);

	// The first instruction of every TH06 ANM script selects a sprite.  The
	// converter stores those sprites by their local atlas index, so retain
	// the original script cadence instead of treating an ANM script as a
	// static texture.
	switch (scriptId) {
	case 0:
		animSpriteIndices = {0, 1, 2, 3};
		animFrameDuration = 4.0f / 60.0f;
		break;
	case 1:
		animSpriteIndices = {4, 5, 6, 7};
		animFrameDuration = 6.0f / 60.0f;
		break;
	case 2:
		animSpriteIndices = {8, 9, 10, 11};
		animFrameDuration = 6.0f / 60.0f;
		break;
	case 3:
		animSpriteIndices = {12, 13, 14, 15};
		animFrameDuration = 6.0f / 60.0f;
		break;
	case 4:
	case 6:
		animSpriteIndices = {16, 17, 18, 19};
		animFrameDuration = 6.0f / 60.0f;
		break;
	case 5:
	case 7:
		animSpriteIndices = {20, 21, 22, 23};
		animFrameDuration = 6.0f / 60.0f;
		break;
	case 10:
		animSpriteIndices = {25, 26, 27, 28};
		animFrameDuration = 3.0f / 60.0f;
		break;
	case 11:
		animSpriteIndices = {29, 30, 31, 32};
		animFrameDuration = 3.0f / 60.0f;
		break;
	case 12:
		animSpriteIndices = {33, 34, 35, 36};
		animFrameDuration = 3.0f / 60.0f;
		break;
	case 67:
	case 68:
		animSpriteIndices = {38, 42, 41, 40};
		animFrameDuration = 5.0f / 60.0f;
		break;
	case 69:
	case 70:
		animSpriteIndices = {40, 41, 42, 38};
		animFrameDuration = 5.0f / 60.0f;
		break;
	case 160:
		animSpriteIndices = {160, 161, 162, 163};
		animFrameDuration = 6.0f / 60.0f;
		break;
	case 161:
	case 162:
		animSpriteIndices = {160, 164, 165};
		animFrameDuration = 4.0f / 60.0f;
		break;
	case 163:
	case 164:
		animSpriteIndices = {165, 164, 160, 161, 162, 163};
		animFrameDuration = 4.0f / 60.0f;
		break;
	case 165:
		animSpriteIndices = {168, 169, 170, 171};
		animFrameDuration = 4.0f / 60.0f;
		break;
	default:
		break;
	}
}

void setSpriteIndex(int32_t idx) {
	if (idx < 0)
		return;
	auto loadAndSet = [&](const std::string &atlas, int localId) -> bool {
		if (!resourceManager)
			return false;
		auto tex = resourceManager->getSpriteTexture(atlas, localId);
		if (tex && tex->isValid()) {
			texture = tex;
			sprite.setTexture(tex);
			float tw = static_cast<float>(tex->getWidth());
			float th = static_cast<float>(tex->getHeight());
			sprite.setSourceRect(shiki::Rect(0, 0, tw, th));
			sprite.setOrigin({tw / 2.0f, th / 2.0f});
			sprite.setScale(1.0f, 1.0f);
			return true;
		}
		return false;
	};
	if (!atlasName.empty()) {
		int localId = static_cast<int>(idx);
		TH06MenuAnmFile animation;
		auto *store = resourceManager ? const_cast<shiki::asset::AssetStore *>(
		                                    resourceManager->getAssetStore())
		                              : nullptr;
		if (animation.load(store, atlasName))
			localId = animation.resolveSprite(localId);
		if (loadAndSet(atlasName, localId))
			return;
	}
	const std::string primaryAtlas =
	    "stg" + std::to_string(stageNumber) + "enm";
	const std::string secondaryAtlas = primaryAtlas + "2";
	if (loadAndSet(primaryAtlas, static_cast<int>(idx)))
		return;
	if (loadAndSet(secondaryAtlas, static_cast<int>(idx)))
		return;
	// Stage 7's second ANM uses the original global base of 160.
	if (stageNumber == 7 && idx >= 160 &&
	    loadAndSet(secondaryAtlas, static_cast<int>(idx) - 160))
		return;
	if (texture && texture->isValid()) {
		float tw = static_cast<float>(texture->getWidth());
		float th = static_cast<float>(texture->getHeight());
		sprite.setSourceRect(shiki::Rect(0, 0, tw, th));
		sprite.setOrigin({tw / 2.0f, th / 2.0f});
	}
}
