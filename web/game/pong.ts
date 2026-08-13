/**
 * Pong:Rush — A complete browser game built on the TD Engine TypeScript port.
 *
 * Showcases:
 *   - The engine's ECS (World, PositionComponent, VelocityComponent, SpriteComponent, ColliderComponent)
 *   - SpriteBatch with rotation, color tint, and alpha
 *   - AABB collision detection (mirrors src/physics/aabb.h)
 *   - Input handling (keyboard via Input/Key)
 *   - Game loop with fixed-step updates (mirrors td::GameLoop)
 *   - Particle effects via ParticleSystem
 *   - Scene state machine (Title → Playing → Scored → GameOver)
 *   - AI opponent with prediction
 *
 * Inspired by examples/pong/main.cpp in the C++ engine.
 */

import { Engine } from '../engine/engine';
import { Color, clamp, lerp } from '../engine/math';
import { ComponentType, componentBit } from '../engine/ecs';
import { Key } from '../engine/input';
import { ParticleSystem } from './particles';

type Scene = 'title' | 'countdown' | 'playing' | 'scored' | 'gameover';

const WIDTH = 800;
const HEIGHT = 600;

const PADDLE_W = 14;
const PADDLE_H = 90;
const PADDLE_SPEED = 460;
const BALL_SIZE = 12;
const BALL_SPEED = 360;
const BALL_SPEED_MAX = 640;
const WALL_H = 10;
const WIN_SCORE = 7;

const COLOR_BG_TOP = Color.rgb(10, 14, 26);
const COLOR_BG_BOT = Color.rgb(24, 18, 40);
const COLOR_PADDLE_P1 = Color.rgb(80, 220, 255);
const COLOR_PADDLE_P2 = Color.rgb(255, 110, 180);
const COLOR_BALL = Color.rgb(255, 240, 200);
const COLOR_WALL = Color.rgb(60, 70, 100);
const COLOR_CENTER = Color.rgb(80, 90, 130);

interface Entities {
  leftPaddle: number;
  rightPaddle: number;
  ball: number;
  topWall: number;
  bottomWall: number;
}

export class PongRush {
  private engine: Engine;
  private particles: ParticleSystem;
  private scene: Scene = 'title';

  private ents!: Entities;
  private leftScore = 0;
  private rightScore = 0;

  private countdown = 0;
  private scoredDelay = 0;
  private shakeAmount = 0;
  private shakeTime = 0;
  private timeScale = 1; // slow-mo on big hits
  private timeScaleTarget = 1;

  private titlePulse = 0;
  private aiReactionTimer = 0;
  private aiTargetY = HEIGHT / 2;

  // Trail history for the ball
  private ballTrail: { x: number; y: number; a: number }[] = [];
  private ballTrailTimer = 0;

  constructor(canvas: HTMLCanvasElement) {
    this.engine = new Engine({
      canvas,
      width: WIDTH,
      height: HEIGHT,
      bgR: COLOR_BG_BOT.r,
      bgG: COLOR_BG_BOT.g,
      bgB: COLOR_BG_BOT.b,
    });
    this.particles = new ParticleSystem(
      this.engine.getWorld(),
      this.engine.getRenderer().getSpriteBatch(),
    );
  }

  init(): void {
    this.engine.init();
    this.engine.setFixedStep(1 / 60);
    this.engine.setCallbacks(null, this.onUpdate, this.onRender);
    this.buildLevel();
    this.engine.start();
  }

  private buildLevel(): void {
    const world = this.engine.getWorld();

    this.ents = {
      leftPaddle: 0,
      rightPaddle: 0,
      ball: 0,
      topWall: 0,
      bottomWall: 0,
    } as Entities;

    // Left paddle (player)
    this.ents.leftPaddle = world.createEntity('LeftPaddle');
    world.setEntityTag(this.ents.leftPaddle, 'paddle');
    world.addPosition(this.ents.leftPaddle, 40, HEIGHT / 2);
    world.addVelocity(this.ents.leftPaddle, 0, 0);
    world.addSprite({
      width: PADDLE_W, height: PADDLE_H,
      r: COLOR_PADDLE_P1.r, g: COLOR_PADDLE_P1.g, b: COLOR_PADDLE_P1.b, a: 1,
    })(this.ents.leftPaddle);
    world.addCollider({ width: PADDLE_W, height: PADDLE_H })(this.ents.leftPaddle);

    // Right paddle (AI)
    this.ents.rightPaddle = world.createEntity('RightPaddle');
    world.setEntityTag(this.ents.rightPaddle, 'paddle');
    world.addPosition(this.ents.rightPaddle, WIDTH - 40, HEIGHT / 2);
    world.addVelocity(this.ents.rightPaddle, 0, 0);
    world.addSprite({
      width: PADDLE_W, height: PADDLE_H,
      r: COLOR_PADDLE_P2.r, g: COLOR_PADDLE_P2.g, b: COLOR_PADDLE_P2.b, a: 1,
    })(this.ents.rightPaddle);
    world.addCollider({ width: PADDLE_W, height: PADDLE_H })(this.ents.rightPaddle);

    // Ball
    this.ents.ball = world.createEntity('Ball');
    world.addPosition(this.ents.ball, WIDTH / 2, HEIGHT / 2);
    world.addVelocity(this.ents.ball, 0, 0);
    world.addSprite({
      width: BALL_SIZE, height: BALL_SIZE,
      r: COLOR_BALL.r, g: COLOR_BALL.g, b: COLOR_BALL.b, a: 1,
    })(this.ents.ball);
    world.addCollider({ width: BALL_SIZE, height: BALL_SIZE })(this.ents.ball);

    // Walls
    this.ents.topWall = world.createEntity('TopWall');
    world.addPosition(this.ents.topWall, WIDTH / 2, WALL_H / 2);
    world.addSprite({
      width: WIDTH, height: WALL_H,
      r: COLOR_WALL.r, g: COLOR_WALL.g, b: COLOR_WALL.b, a: 1,
    })(this.ents.topWall);
    world.addCollider({ width: WIDTH, height: WALL_H })(this.ents.topWall);

    this.ents.bottomWall = world.createEntity('BottomWall');
    world.addPosition(this.ents.bottomWall, WIDTH / 2, HEIGHT - WALL_H / 2);
    world.addSprite({
      width: WIDTH, height: WALL_H,
      r: COLOR_WALL.r, g: COLOR_WALL.g, b: COLOR_WALL.b, a: 1,
    })(this.ents.bottomWall);
    world.addCollider({ width: WIDTH, height: WALL_H })(this.ents.bottomWall);
  }

  private resetBall(direction: number): void {
    const pos = this.engine.getWorld().getPosition(this.ents.ball);
    const vel = this.engine.getWorld().getVelocity(this.ents.ball);
    if (!pos || !vel) return;
    pos.x = WIDTH / 2;
    pos.y = HEIGHT / 2;
    const angle = (Math.random() - 0.5) * 0.6; // ±~17deg
    vel.vx = direction * BALL_SPEED * Math.cos(angle);
    vel.vy = BALL_SPEED * Math.sin(angle);
    this.ballTrail.length = 0;
  }

  private startCountdown(): void {
    this.scene = 'countdown';
    this.countdown = 2.4;
    this.resetBall(Math.random() < 0.5 ? 1 : -1);
    const ballVel = this.engine.getWorld().getVelocity(this.ents.ball);
    if (ballVel) { ballVel.vx = 0; ballVel.vy = 0; }
  }

  private startGame(): void {
    this.leftScore = 0;
    this.rightScore = 0;
    this.startCountdown();
  }

  // ---- Update ----
  private onUpdate = (dtRaw: number) => {
    // smooth time-scale recover
    this.timeScale = lerp(this.timeScale, this.timeScaleTarget, Math.min(1, dtRaw * 6));
    const dt = dtRaw * this.timeScale;

    this.titlePulse += dtRaw;
    this.particles.update(dt);

    // Shake decay
    if (this.shakeTime > 0) {
      this.shakeTime -= dtRaw;
      if (this.shakeTime <= 0) this.shakeAmount = 0;
    }

    switch (this.scene) {
      case 'title': this.updateTitle(dtRaw); break;
      case 'countdown': this.updateCountdown(dt); break;
      case 'playing': this.updatePlaying(dt); break;
      case 'scored': this.updateScored(dt); break;
      case 'gameover': this.updateGameOver(dtRaw); break;
    }
  };

  private updateTitle(_dt: number): void {
    if (this.engine.getInput().keyPressed(Key.Space) ||
        this.engine.getInput().keyPressed(Key.Enter)) {
      this.startGame();
    }
  }

  private updateCountdown(dt: number): void {
    this.countdown -= dt;
    if (this.countdown <= 0) {
      this.scene = 'playing';
      this.resetBall(Math.random() < 0.5 ? 1 : -1);
    }
  }

  private updateScored(dt: number): void {
    this.scoredDelay -= dt;
    if (this.scoredDelay <= 0) {
      if (this.leftScore >= WIN_SCORE || this.rightScore >= WIN_SCORE) {
        this.scene = 'gameover';
      } else {
        this.startCountdown();
      }
    }
  }

  private updateGameOver(_dt: number): void {
    if (this.engine.getInput().keyPressed(Key.Space) ||
        this.engine.getInput().keyPressed(Key.Enter) ||
        this.engine.getInput().keyPressed(Key.R)) {
      this.scene = 'title';
    }
  }

  private updatePlaying(dt: number): void {
    const input = this.engine.getInput();
    const world = this.engine.getWorld();

    // Player input (W/S or Up/Down)
    const leftVel = world.getVelocity(this.ents.leftPaddle);
    if (leftVel) {
      leftVel.vy = 0;
      if (input.key(Key.W) || input.key(Key.Up)) leftVel.vy = -PADDLE_SPEED;
      if (input.key(Key.S) || input.key(Key.Down)) leftVel.vy = PADDLE_SPEED;
    }

    // AI: predict ball y when it reaches right paddle x, with reaction delay
    this.aiReactionTimer -= dt;
    const ballPos = world.getPosition(this.ents.ball);
    const ballVel = world.getVelocity(this.ents.ball);
    const rightPos = world.getPosition(this.ents.rightPaddle);
    const rightVel = world.getVelocity(this.ents.rightPaddle);
    if (ballPos && ballVel && rightPos && rightVel) {
      if (this.aiReactionTimer <= 0 && ballVel.vx > 0) {
        // Predict where the ball will be when it reaches rightPos.x
        const timeToReach = (rightPos.x - ballPos.x) / ballVel.vx;
        let predY = ballPos.y + ballVel.vy * timeToReach;
        // Bounce predictions off walls
        const span = HEIGHT - WALL_H * 2;
        predY = predY - WALL_H;
        predY = ((predY % (2 * span)) + 2 * span) % (2 * span);
        if (predY > span) predY = 2 * span - predY;
        predY += WALL_H;
        // Add a bit of error to keep it beatable
        this.aiTargetY = predY + (Math.random() - 0.5) * 40;
        this.aiReactionTimer = 0.12;
      } else if (ballVel.vx <= 0) {
        // Drift toward center when ball is going away
        this.aiTargetY = HEIGHT / 2 + Math.sin(this.titlePulse * 0.7) * 60;
      }

      const diff = this.aiTargetY - rightPos.y;
      const maxStep = PADDLE_SPEED * 0.92 * dt;
      const step = clamp(diff, -maxStep, maxStep);
      rightVel.vy = step / dt;
    }

    // Integrate velocities → positions for paddles + ball
    const moveEntities = [this.ents.leftPaddle, this.ents.rightPaddle, this.ents.ball];
    for (const id of moveEntities) {
      const p = world.getPosition(id);
      const v = world.getVelocity(id);
      if (p && v) {
        p.x += v.vx * dt;
        p.y += v.vy * dt;
      }
    }

    // Clamp paddles
    const clampPaddle = (id: number) => {
      const p = world.getPosition(id);
      if (!p) return;
      const minY = WALL_H + PADDLE_H / 2;
      const maxY = HEIGHT - WALL_H - PADDLE_H / 2;
      if (p.y < minY) { p.y = minY; }
      if (p.y > maxY) { p.y = maxY; }
    };
    clampPaddle(this.ents.leftPaddle);
    clampPaddle(this.ents.rightPaddle);

    // Ball — wall bounce
    const bp = world.getPosition(this.ents.ball);
    const bv = world.getVelocity(this.ents.ball);
    if (bp && bv) {
      // Top
      if (bp.y - BALL_SIZE / 2 < WALL_H) {
        bp.y = WALL_H + BALL_SIZE / 2;
        bv.vy = Math.abs(bv.vy);
        this.spawnHitParticles(bp.x, WALL_H, 0, 1, COLOR_WALL);
        this.shake(2, 0.08);
      }
      // Bottom
      if (bp.y + BALL_SIZE / 2 > HEIGHT - WALL_H) {
        bp.y = HEIGHT - WALL_H - BALL_SIZE / 2;
        bv.vy = -Math.abs(bv.vy);
        this.spawnHitParticles(bp.x, HEIGHT - WALL_H, 0, -1, COLOR_WALL);
        this.shake(2, 0.08);
      }

      // Left paddle collision
      const lp = world.getPosition(this.ents.leftPaddle);
      if (lp && bv.vx < 0) {
        const ballLeft = bp.x - BALL_SIZE / 2;
        const paddleRight = lp.x + PADDLE_W / 2;
        if (ballLeft < paddleRight && bp.x > lp.x - PADDLE_W / 2 - BALL_SIZE &&
            bp.y > lp.y - PADDLE_H / 2 - BALL_SIZE / 2 &&
            bp.y < lp.y + PADDLE_H / 2 + BALL_SIZE / 2) {
          bp.x = lp.x + PADDLE_W / 2 + BALL_SIZE / 2 + 0.5;
          bv.vx = Math.abs(bv.vx) * 1.06;
          const hitPos = (bp.y - lp.y) / (PADDLE_H / 2); // -1..1
          bv.vy += hitPos * 180;
          this.clampBallSpeed(bv);
          this.spawnHitParticles(bp.x, bp.y, 1, hitPos, COLOR_PADDLE_P1);
          this.shake(4, 0.1);
          this.timeScaleTarget = 0.85;
          setTimeout(() => { this.timeScaleTarget = 1; }, 80);
        }
      }

      // Right paddle collision
      const rp = world.getPosition(this.ents.rightPaddle);
      if (rp && bv.vx > 0) {
        const ballRight = bp.x + BALL_SIZE / 2;
        const paddleLeft = rp.x - PADDLE_W / 2;
        if (ballRight > paddleLeft && bp.x < rp.x + PADDLE_W / 2 + BALL_SIZE &&
            bp.y > rp.y - PADDLE_H / 2 - BALL_SIZE / 2 &&
            bp.y < rp.y + PADDLE_H / 2 + BALL_SIZE / 2) {
          bp.x = rp.x - PADDLE_W / 2 - BALL_SIZE / 2 - 0.5;
          bv.vx = -Math.abs(bv.vx) * 1.06;
          const hitPos = (bp.y - rp.y) / (PADDLE_H / 2);
          bv.vy += hitPos * 180;
          this.clampBallSpeed(bv);
          this.spawnHitParticles(bp.x, bp.y, -1, hitPos, COLOR_PADDLE_P2);
          this.shake(4, 0.1);
          this.timeScaleTarget = 0.85;
          setTimeout(() => { this.timeScaleTarget = 1; }, 80);
        }
      }

      // Scoring
      if (bp.x < -BALL_SIZE) {
        this.rightScore++;
        this.onScore(-1);
      } else if (bp.x > WIDTH + BALL_SIZE) {
        this.leftScore++;
        this.onScore(1);
      }

      // Ball trail
      this.ballTrailTimer -= dt;
      if (this.ballTrailTimer <= 0) {
        this.ballTrail.push({ x: bp.x, y: bp.y, a: 0.6 });
        this.ballTrailTimer = 0.018;
        if (this.ballTrail.length > 14) this.ballTrail.shift();
      }
    }
  }

  private clampBallSpeed(v: { vx: number; vy: number }): void {
    const sp = Math.sqrt(v.vx * v.vx + v.vy * v.vy);
    if (sp > BALL_SPEED_MAX) {
      const k = BALL_SPEED_MAX / sp;
      v.vx *= k; v.vy *= k;
    }
  }

  private onScore(winnerSide: 1 | -1): void {
    this.scene = 'scored';
    this.scoredDelay = 1.2;
    const ballPos = this.engine.getWorld().getPosition(this.ents.ball);
    const ballVel = this.engine.getWorld().getVelocity(this.ents.ball);
    if (ballPos && ballVel) {
      ballVel.vx = 0; ballVel.vy = 0;
      const col = winnerSide === 1 ? COLOR_PADDLE_P1 : COLOR_PADDLE_P2;
      this.particles.burst({
        count: 60,
        x: ballPos.x, y: ballPos.y,
        speedMin: 80, speedMax: 360,
        angleMin: 0, angleMax: Math.PI * 2,
        sizeMin: 2, sizeMax: 6,
        r: col.r, g: col.g, b: col.b,
        lifeMin: 0.4, lifeMax: 1.2,
        drag: 0.94,
      });
    }
    this.shake(8, 0.3);
  }

  private spawnHitParticles(x: number, y: number, dirX: number, dirY: number, baseColor: Color): void {
    const baseAngle = Math.atan2(dirY, dirX);
    this.particles.burst({
      count: 14,
      x, y,
      speedMin: 60, speedMax: 220,
      angleMin: baseAngle - 0.8,
      angleMax: baseAngle + 0.8,
      sizeMin: 1.5, sizeMax: 3.5,
      r: baseColor.r, g: baseColor.g, b: baseColor.b,
      lifeMin: 0.2, lifeMax: 0.5,
      drag: 0.9,
    });
  }

  private shake(amount: number, time: number): void {
    this.shakeAmount = Math.max(this.shakeAmount, amount);
    this.shakeTime = Math.max(this.shakeTime, time);
  }

  // ---- Render ----
  private onRender = (alpha: number) => {
    void alpha;
    const renderer = this.engine.getRenderer();
    const camera = this.engine.getCamera();
    const batch = renderer.getSpriteBatch();

    // Screen shake offset
    let shakeX = 0, shakeY = 0;
    if (this.shakeAmount > 0 && this.shakeTime > 0) {
      const k = this.shakeTime * this.shakeAmount;
      shakeX = (Math.random() - 0.5) * k;
      shakeY = (Math.random() - 0.5) * k;
    }
    this.engine.getCamera().setPosition(shakeX, shakeY);

    const proj = camera.getProjection();
    const view = camera.getView();

    batch.begin(proj, view);

    this.drawBackground();
    this.drawCenterLine();
    this.drawEntities();
    this.drawBallTrail();
    this.particles.draw();
    this.drawHUD();

    batch.end();

    // Overlay text is drawn by React/CSS layer above the canvas
  };

  private drawBackground(): void {
    const batch = this.engine.getRenderer().getSpriteBatch();
    // gradient stripes (cheap fake gradient)
    const stripes = 24;
    for (let i = 0; i < stripes; i++) {
      const t = i / (stripes - 1);
      const c = Color.rgb(
        Math.round(lerp(COLOR_BG_TOP.r * 255, COLOR_BG_BOT.r * 255, t)),
        Math.round(lerp(COLOR_BG_TOP.g * 255, COLOR_BG_BOT.g * 255, t)),
        Math.round(lerp(COLOR_BG_TOP.b * 255, COLOR_BG_BOT.b * 255, t)),
      );
      batch.drawQuad(0, i * (HEIGHT / stripes), WIDTH, HEIGHT / stripes + 1, c.r, c.g, c.b, 1);
    }
    // subtle vignette
    batch.drawQuad(0, 0, WIDTH, 80, 0, 0, 0, 0.35);
    batch.drawQuad(0, HEIGHT - 80, WIDTH, 80, 0, 0, 0, 0.35);
  }

  private drawCenterLine(): void {
    const batch = this.engine.getRenderer().getSpriteBatch();
    const segments = 20;
    for (let i = 0; i < segments; i++) {
      if (i % 2 === 0) {
        const y = i * (HEIGHT / segments);
        batch.drawQuad(WIDTH / 2 - 2, y, 4, HEIGHT / segments - 4,
          COLOR_CENTER.r, COLOR_CENTER.g, COLOR_CENTER.b, 0.7);
      }
    }
  }

  private drawEntities(): void {
    const batch = this.engine.getRenderer().getSpriteBatch();
    const world = this.engine.getWorld();
    const mask = componentBit(ComponentType.Position) | componentBit(ComponentType.Sprite);
    const ids = world.queryActive(mask);
    for (const id of ids) {
      const pos = world.getPosition(id);
      const sprite = world.getSprite(id);
      if (!pos || !sprite || !sprite.visible) continue;
      if (id === this.ents.ball) continue; // ball drawn separately with trail
      batch.draw({
        x: pos.x - sprite.width * sprite.originX,
        y: pos.y - sprite.height * sprite.originY,
        width: sprite.width, height: sprite.height,
        u0: 0, v0: 0, u1: 1, v1: 1,
        r: sprite.r, g: sprite.g, b: sprite.b, a: sprite.a,
        rotation: sprite.rotation, originX: sprite.originX, originY: sprite.originY,
      }, sprite.texture);
    }

    // Ball with pulsing glow when ball is fast
    const bp = world.getPosition(this.ents.ball);
    const bs = world.getSprite(this.ents.ball);
    const bv = world.getVelocity(this.ents.ball);
    if (bp && bs && bv) {
      const sp = Math.sqrt(bv.vx * bv.vx + bv.vy * bv.vy);
      const heat = clamp((sp - BALL_SPEED) / (BALL_SPEED_MAX - BALL_SPEED), 0, 1);
      // Glow halo
      const glowSize = BALL_SIZE + 8 + heat * 14;
      batch.drawQuad(
        bp.x - glowSize / 2, bp.y - glowSize / 2,
        glowSize, glowSize,
        lerp(COLOR_BALL.r, 1, heat),
        lerp(COLOR_BALL.g, 0.4, heat),
        lerp(COLOR_BALL.b, 0.2, heat),
        0.25 + heat * 0.4,
      );
      // Ball itself
      batch.drawQuad(
        bp.x - BALL_SIZE / 2, bp.y - BALL_SIZE / 2,
        BALL_SIZE, BALL_SIZE,
        bs.r, bs.g, bs.b, bs.a,
      );
    }
  }

  private drawBallTrail(): void {
    const batch = this.engine.getRenderer().getSpriteBatch();
    for (let i = 0; i < this.ballTrail.length; i++) {
      const t = this.ballTrail[i];
      const fade = (i + 1) / this.ballTrail.length;
      const size = BALL_SIZE * fade;
      batch.drawQuad(
        t.x - size / 2, t.y - size / 2,
        size, size,
        COLOR_BALL.r, COLOR_BALL.g, COLOR_BALL.b,
        0.25 * fade,
      );
    }
  }

  private drawHUD(): void {
    const batch = this.engine.getRenderer().getSpriteBatch();
    // Score bars at top
    const half = WIDTH / 2;
    const barH = 6;
    const barY = 24;
    const barMax = half - 60;

    // Left score (player) - left aligned bar growing right
    const leftFill = (this.leftScore / WIN_SCORE) * barMax;
    batch.drawQuad(40, barY, barMax, barH, 0.2, 0.2, 0.3, 1);
    batch.drawQuad(40, barY, leftFill, barH,
      COLOR_PADDLE_P1.r, COLOR_PADDLE_P1.g, COLOR_PADDLE_P1.b, 1);

    // Right score (AI) - right aligned bar growing left
    const rightFill = (this.rightScore / WIN_SCORE) * barMax;
    batch.drawQuad(WIDTH - 40 - barMax, barY, barMax, barH, 0.2, 0.2, 0.3, 1);
    batch.drawQuad(WIDTH - 40 - rightFill, barY, rightFill, barH,
      COLOR_PADDLE_P2.r, COLOR_PADDLE_P2.g, COLOR_PADDLE_P2.b, 1);

    // Numeric score blocks
    const blockW = 18, blockH = 28;
    this.drawDigit(this.leftScore, half - 50 - blockW, 16, blockW, blockH, COLOR_PADDLE_P1);
    this.drawDigit(this.rightScore, half + 50, 16, blockW, blockH, COLOR_PADDLE_P2);
  }

  private drawDigit(n: number, x: number, y: number, w: number, h: number, color: Color): void {
    const batch = this.engine.getRenderer().getSpriteBatch();
    n = clamp(n, 0, 9);
    // Render using 7-seg-like bit pattern (cheap)
    const segs = this.sevenSeg(n);
    const t = h / 2; // segment thickness
    const segH = (h - t * 3) / 2;
    // 0=top, 1=top-left, 2=top-right, 3=mid, 4=bot-left, 5=bot-right, 6=bot
    const x0 = x, x1 = x + w;
    const yT = y, yM = y + t + segH, yB = y + h - t;
    const draws: [number, number, number, number, boolean][] = [
      [x0 + t, yT, w - t * 2, t, segs[0]], // top
      [x0, yT + t, t, segH, segs[1]],      // top-left
      [x1 - t, yT + t, t, segH, segs[2]],  // top-right
      [x0 + t, yM, w - t * 2, t, segs[3]], // mid
      [x0, yM + t, t, segH, segs[4]],      // bot-left
      [x1 - t, yM + t, t, segH, segs[5]],  // bot-right
      [x0 + t, yB, w - t * 2, t, segs[6]], // bot
    ];
    for (const [dx, dy, dw, dh, on] of draws) {
      if (on) {
        batch.drawQuad(dx, dy, dw, dh, color.r, color.g, color.b, 1);
      } else {
        batch.drawQuad(dx, dy, dw, dh, color.r * 0.15, color.g * 0.15, color.b * 0.15, 0.4);
      }
    }
  }

  private sevenSeg(n: number): boolean[] {
    const map: Record<number, boolean[]> = {
      0: [true,  true,  true,  false, true,  true,  true],
      1: [false, false, true,  false, false, true,  false],
      2: [true,  false, true,  true,  true,  false, true],
      3: [true,  false, true,  true,  false, true,  true],
      4: [false, true,  true,  true,  false, true,  false],
      5: [true,  true,  false, true,  false, true,  true],
      6: [true,  true,  false, true,  true,  true,  true],
      7: [true,  false, true,  false, false, true,  false],
      8: [true,  true,  true,  true,  true,  true,  true],
      9: [true,  true,  true,  true,  false, true,  true],
    };
    return map[n] ?? map[0];
  }

  // ---- React-side query API ----
  getScene(): Scene { return this.scene; }
  getLeftScore(): number { return this.leftScore; }
  getRightScore(): number { return this.rightScore; }
  getCountdown(): number { return Math.ceil(this.countdown); }
  getTitlePulse(): number { return this.titlePulse; }
  getWinScore(): number { return WIN_SCORE; }

  startFromReact(): void {
    if (this.scene === 'title' || this.scene === 'gameover') {
      this.startGame();
    }
  }

  shutdown(): void {
    this.engine.shutdown();
  }
}
