// TS smoke test — verify td_api.d.ts + TDScript typings compile cleanly.
// Run: npx tsc -p tests/ts/tsconfig.json

/// <reference path="../../web/td_api.d.ts" />

// --- TDEngine API smoke --------------------------------------------------
async function engineSmoke(): Promise<void> {
  await TDEngine.lifecycle.init('game-canvas');
  const id: number = TDEngine.ecs.create('Player');
  TDEngine.ecs.setPosition(id, 100, 200);
  const isDown: boolean = TDEngine.input.isKeyDown(0x41);
  if (isDown) TDEngine.audio.resume();
  const version: string = TDEngine.version;
  // Touch the new TDScript runtime typings:
  const runtime = TDScriptRuntime;
  const rpcTable: Map<string, { mode: 'reliable' | 'unreliable'; fn: (i: any, a: any[]) => any }> = runtime.rpcTable;
  void rpcTable;
  void version;
}

// --- TDScript runtime smoke ----------------------------------------------
function scriptSmoke(): void {
  // Globals provided by tdscript_runtime.js
  const v = new Vector3(1, 2, 3);
  const v2 = v.add(new Vector3(0, 1, 0)).mul(2);
  const L: number = v2.length();
  if (L > 0) Log.info('Vector length: ' + L);

  // Network
  Network.broadcastNotification('hello');
  Network.broadcastState('ServerMain.playerHealth', 100);

  // Physics
  const solid: boolean = Physics.checkVoxelCollision(v);

  // RPC registration
  __td_rpc_register('ServerMain', 'processPlayerDamage', 'reliable', (instance, args) => {
    instance.hp = instance.hp - args[0];
  });

  // Replication registration
  __td_repl_register('ServerMain', ['playerPosition', 'playerHealth']);

  // Entry hook
  const inst: any = __td_script_main('ServerMain');
  void inst;
  void solid;
}

// --- Bootstrap smoke -----------------------------------------------------
async function bootstrapSmoke(): Promise<void> {
  const ws: WebSocket | null = await TDClientBootstrap.bootstrap();
  void ws;
}

void engineSmoke;
void scriptSmoke;
void bootstrapSmoke;
