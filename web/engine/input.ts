/**
 * TD Engine — Input (TypeScript port of src/platform/win32_input.h)
 * Same Key enum & InputState shape as the C++ engine.
 */

export enum Key {
  Backspace = 8,
  Tab = 9,
  Enter = 13,
  Shift = 16,
  Ctrl = 17,
  Alt = 18,
  Escape = 27,
  Space = 32,
  PageUp = 33,
  PageDown = 34,
  End = 35,
  Home = 36,
  Left = 37,
  Up = 38,
  Right = 39,
  Down = 40,
  Insert = 45,
  Delete = 46,
  Num0 = 48,
  Num1 = 49,
  Num2 = 50,
  Num3 = 51,
  Num4 = 52,
  Num5 = 53,
  Num6 = 54,
  Num7 = 55,
  Num8 = 56,
  Num9 = 57,
  A = 65,
  B = 66,
  C = 67,
  D = 68,
  E = 69,
  F = 70,
  G = 71,
  H = 72,
  I = 73,
  J = 74,
  K = 75,
  L = 76,
  M = 77,
  N = 78,
  O = 79,
  P = 80,
  Q = 81,
  R = 82,
  S = 83,
  T = 84,
  U = 85,
  V = 86,
  W = 87,
  X = 88,
  Y = 89,
  Z = 90,
  F1 = 112,
  F2 = 113,
  F3 = 114,
  F4 = 115,
  F5 = 116,
  F6 = 117,
  F7 = 118,
  F8 = 119,
  F9 = 120,
  F10 = 121,
  F11 = 122,
  F12 = 123,
}

export interface MouseState {
  x: number;
  y: number;
  dx: number;
  dy: number;
  buttons: boolean[];
}

export interface InputState {
  keys: boolean[];
  keysPressed: boolean[]; // edge: pressed this frame
  keysReleased: boolean[]; // edge: released this frame
  mouse: MouseState;
}

export class Input implements InputState {
  keys: boolean[] = new Array(256).fill(false);
  keysPressed: boolean[] = new Array(256).fill(false);
  keysReleased: boolean[] = new Array(256).fill(false);
  mouse = {
    x: 0,
    y: 0,
    dx: 0,
    dy: 0,
    buttons: new Array(8).fill(false),
  };

  private attached = false;
  private canvas: HTMLCanvasElement | null = null;

  attach(canvas: HTMLCanvasElement): void {
    if (this.attached) return;
    this.attached = true;
    this.canvas = canvas;

    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
    canvas.addEventListener('mousemove', this.onMouseMove);
    canvas.addEventListener('mousedown', this.onMouseDown);
    window.addEventListener('mouseup', this.onMouseUp);
    canvas.addEventListener('contextmenu', this.onContextMenu);
  }

  detach(): void {
    if (!this.attached) return;
    this.attached = false;
    window.removeEventListener('keydown', this.onKeyDown);
    window.removeEventListener('keyup', this.onKeyUp);
    this.canvas?.removeEventListener('mousemove', this.onMouseMove);
    this.canvas?.removeEventListener('mousedown', this.onMouseDown);
    window.removeEventListener('mouseup', this.onMouseUp);
    this.canvas?.removeEventListener('contextmenu', this.onContextMenu);
    this.canvas = null;
  }

  private onKeyDown = (e: KeyboardEvent) => {
    if (e.keyCode >= 0 && e.keyCode < 256) {
      if (!this.keys[e.keyCode]) this.keysPressed[e.keyCode] = true;
      this.keys[e.keyCode] = true;
    }
    if ([37, 38, 39, 40, 32].includes(e.keyCode)) e.preventDefault();
  };

  private onKeyUp = (e: KeyboardEvent) => {
    if (e.keyCode >= 0 && e.keyCode < 256) {
      this.keysReleased[e.keyCode] = true;
      this.keys[e.keyCode] = false;
    }
  };

  private onMouseMove = (e: MouseEvent) => {
    if (!this.canvas) return;
    const rect = this.canvas.getBoundingClientRect();
    const nx = e.clientX - rect.left;
    const ny = e.clientY - rect.top;
    this.mouse.dx = nx - this.mouse.x;
    this.mouse.dy = ny - this.mouse.y;
    this.mouse.x = nx;
    this.mouse.y = ny;
  };

  private onMouseDown = (e: MouseEvent) => {
    if (e.button >= 0 && e.button < 8) this.mouse.buttons[e.button] = true;
  };

  private onMouseUp = (e: MouseEvent) => {
    if (e.button >= 0 && e.button < 8) this.mouse.buttons[e.button] = false;
  };

  private onContextMenu = (e: Event) => {
    e.preventDefault();
  };

  /** Call at end of frame to clear edge-triggered state. */
  endFrame(): void {
    this.keysPressed.fill(false);
    this.keysReleased.fill(false);
    this.mouse.dx = 0;
    this.mouse.dy = 0;
  }

  key(k: Key): boolean {
    return this.keys[k];
  }

  keyPressed(k: Key): boolean {
    return this.keysPressed[k];
  }

  keyReleased(k: Key): boolean {
    return this.keysReleased[k];
  }
}
