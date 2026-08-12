/** Pi extension exposing one Python agentloop Round as tools and slash commands. */

import type { ExtensionAPI, ExtensionContext } from "@earendil-works/pi-coding-agent";
import { StringEnum } from "@earendil-works/pi-ai";
import { Type } from "typebox";
import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { existsSync } from "node:fs";
import { delimiter, dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const Action = StringEnum([
	"start",
	"status",
	"wait",
	"pause",
	"resume",
	"end",
	"edit",
	"join",
] as const);

const Gate = StringEnum(["continue", "pause"] as const);

const Params = Type.Object({
	action: Action,
	prompt: Type.Optional(Type.String()),
	images: Type.Optional(Type.Array(Type.String())),
	auto_finish: Type.Optional(Type.Boolean()),
	gates: Type.Optional(Type.Object({
		after_step: Type.Optional(Gate),
		after_tools: Type.Optional(Gate),
	})),
	config: Type.Optional(Type.Record(Type.String(), Type.Unknown())),
	after: Type.Optional(Type.Integer({ minimum: 0 })),
	timeout: Type.Optional(Type.Number({ minimum: 0, maximum: 60 })),
	reason: Type.Optional(Type.String()),
	set: Type.Optional(Type.Record(Type.String(), Type.Unknown())),
	expected_step: Type.Optional(Type.Integer({ minimum: 0 })),
	expected_state: Type.Optional(Type.String()),
});

interface Pending {
	resolve: (value: unknown) => void;
	reject: (error: Error) => void;
	timer: ReturnType<typeof setTimeout>;
	onAbort?: () => void;
	signal?: AbortSignal;
}

class BridgeClient {
	private child: ChildProcessWithoutNullStreams | null = null;
	private pending = new Map<number, Pending>();
	private nextId = 1;
	private stdoutBuffer = "";
	private closing = false;
	private lastStderr = "";

	async request(payload: Record<string, unknown>, signal?: AbortSignal): Promise<unknown> {
		const child = this.ensureChild();
		const id = this.nextId++;
		const timeoutSeconds = payload.action === "wait"
			? Number(payload.timeout ?? 10) + 2
			: 12;
		return new Promise((resolvePromise, rejectPromise) => {
			const timer = setTimeout(() => {
				this.pending.delete(id);
				rejectPromise(new Error(`agentloop bridge request timed out: ${payload.action}`));
			}, timeoutSeconds * 1000);
			const pending: Pending = { resolve: resolvePromise, reject: rejectPromise, timer, signal };
			if (signal) {
				pending.onAbort = () => {
					if (!this.pending.delete(id)) return;
					clearTimeout(timer);
					rejectPromise(new Error("agentloop bridge request aborted"));
				};
				signal.addEventListener("abort", pending.onAbort, { once: true });
			}
			this.pending.set(id, pending);
			child.stdin.write(`${JSON.stringify({ id, ...payload })}\n`, (error) => {
				if (error) this.rejectPending(id, error);
			});
		});
	}

	async close(): Promise<void> {
		if (!this.child || this.closing) return;
		this.closing = true;
		try {
			await this.request({ action: "shutdown", reason: "pi_shutdown", timeout: 2 });
		} catch {
			// The grace period is best effort; process exit is the final fallback.
		}
		this.child?.stdin.end();
		const child = this.child;
		if (child && child.exitCode === null) {
			setTimeout(() => {
				if (child.exitCode === null) child.kill();
			}, 500).unref();
		}
	}

	private ensureChild(): ChildProcessWithoutNullStreams {
		if (this.child && this.child.exitCode === null) return this.child;
		if (this.closing) throw new Error("agentloop bridge is closing");

		const scriptsDir = dirname(fileURLToPath(import.meta.url));
		const bridgePath = join(scriptsDir, "pi_bridge.py");
		const freepyRoot = resolve(scriptsDir, "../..");
		const configuredPython = process.env.AGENTLOOP_PI_PYTHON;
		const posixVenv = join(freepyRoot, ".venv", "bin", "python");
		const windowsVenv = join(freepyRoot, ".venv", "Scripts", "python.exe");
		const python = configuredPython
			?? (existsSync(posixVenv) ? posixVenv : existsSync(windowsVenv) ? windowsVenv : "python3");
		const args = [bridgePath];
		const factory = process.env.AGENTLOOP_PI_FACTORY;
		if (factory) args.push("--factory", factory);
		const pythonPath = [process.cwd(), freepyRoot, join(freepyRoot, "llmkit"), process.env.PYTHONPATH]
			.filter((part): part is string => Boolean(part))
			.join(delimiter);

		const child = spawn(python, args, {
			cwd: process.cwd(),
			env: { ...process.env, PYTHONPATH: pythonPath },
			stdio: ["pipe", "pipe", "pipe"],
		});
		this.child = child;
		this.stdoutBuffer = "";
		this.lastStderr = "";
		child.stdout.setEncoding("utf8");
		child.stderr.setEncoding("utf8");
		child.stdout.on("data", (chunk: string) => this.consumeStdout(chunk));
		child.stderr.on("data", (chunk: string) => {
			this.lastStderr = (this.lastStderr + chunk).slice(-4000);
		});
		child.on("error", (error) => this.rejectAll(error));
		child.on("exit", (code, signal) => {
			const detail = this.lastStderr.trim();
			const suffix = detail ? `\n${detail}` : "";
			this.rejectAll(new Error(
				`agentloop bridge exited (code=${code}, signal=${signal})${suffix}`,
			));
			if (this.child === child) this.child = null;
		});
		return child;
	}

	private consumeStdout(chunk: string): void {
		this.stdoutBuffer += chunk;
		while (true) {
			const newline = this.stdoutBuffer.indexOf("\n");
			if (newline < 0) return;
			const line = this.stdoutBuffer.slice(0, newline);
			this.stdoutBuffer = this.stdoutBuffer.slice(newline + 1);
			if (!line) continue;
			let message: any;
			try {
				message = JSON.parse(line);
			} catch {
				this.rejectAll(new Error(`invalid JSONL from agentloop bridge: ${line.slice(0, 200)}`));
				this.child?.kill();
				return;
			}
			const pending = this.pending.get(message.id);
			if (!pending) continue;
			this.pending.delete(message.id);
			clearTimeout(pending.timer);
			if (pending.onAbort) pending.signal?.removeEventListener("abort", pending.onAbort);
			if (message.ok) {
				pending.resolve(message.result);
			} else {
				pending.reject(new Error(message.error?.message ?? "agentloop bridge error"));
			}
		}
	}

	private rejectPending(id: number, error: Error): void {
		const pending = this.pending.get(id);
		if (!pending) return;
		this.pending.delete(id);
		clearTimeout(pending.timer);
		if (pending.onAbort) pending.signal?.removeEventListener("abort", pending.onAbort);
		pending.reject(error);
	}

	private rejectAll(error: Error): void {
		for (const id of [...this.pending.keys()]) this.rejectPending(id, error);
	}
}

function cleanPayload(params: Record<string, unknown>): Record<string, unknown> {
	return Object.fromEntries(Object.entries(params).filter(([, value]) => value !== undefined));
}

function render(value: unknown): string {
	return JSON.stringify(value, null, 2);
}

async function command(
	client: BridgeClient,
	ctx: ExtensionContext,
	payload: Record<string, unknown>,
): Promise<boolean> {
	try {
		const result = await client.request(payload);
		ctx.ui.notify(render(result), "info");
		return true;
	} catch (error) {
		ctx.ui.notify(error instanceof Error ? error.message : String(error), "error");
		return false;
	}
}

export default function agentloopExtension(pi: ExtensionAPI) {
	const client = new BridgeClient();

	pi.registerTool({
		name: "agentloop_control",
		label: "Agentloop Control",
		description: "Start, inspect, wait for, edit, pause, resume, or safely end one Python agentloop Round.",
		promptSnippet: "Control the external Python agentloop Round",
		promptGuidelines: [
			"Use agentloop_control only when the user wants to operate the external agentloop Round.",
			"Use status or wait before editing; pass expected_step/expected_state to reject stale edits.",
			"Editing does not resume a paused Round; call resume explicitly when the user wants execution to continue.",
		],
		parameters: Params,
		executionMode: "sequential",
		async execute(_toolCallId, params, signal) {
			try {
				const result = await client.request(cleanPayload(params), signal);
				return {
					content: [{ type: "text", text: render(result) }],
					details: result,
				};
			} catch (error) {
				const message = error instanceof Error ? error.message : String(error);
				return {
					content: [{ type: "text", text: `Error: ${message}` }],
					details: { error: message },
				};
			}
		},
	});

	pi.registerCommand("al-start", {
		description: "Start one agentloop Round (argument is the initial prompt)",
		handler: (args, ctx) => command(client, ctx, {
			action: "start",
			prompt: args.trim() || "Start",
		}),
	});
	pi.registerCommand("al-status", {
		description: "Show the current agentloop snapshot",
		handler: (_args, ctx) => command(client, ctx, { action: "status" }),
	});
	pi.registerCommand("al-wait", {
		description: "Wait for the next agentloop boundary event",
		handler: (args, ctx) => command(client, ctx, {
			action: "wait",
			timeout: args.trim() ? Number(args) : 10,
		}),
	});
	pi.registerCommand("al-pause", {
		description: "Request a pause at the next safe boundary",
		handler: (_args, ctx) => command(client, ctx, { action: "pause" }),
	});
	pi.registerCommand("al-resume", {
		description: "Resume a waiting or paused agentloop Round",
		handler: async (args, ctx) => {
			if (args.trim()) {
				const edited = await command(client, ctx, {
					action: "edit",
					set: { prompt: args.trim() },
				});
				if (!edited) return;
			}
			await command(client, ctx, { action: "resume" });
		},
	});
	pi.registerCommand("al-end", {
		description: "Safely end the current agentloop Round",
		handler: (args, ctx) => command(client, ctx, {
			action: "end",
			reason: args.trim() || "pi_operator",
		}),
	});

	pi.on("session_shutdown", async () => {
		await client.close();
	});
}
