// The optional "target" launch-config block: a host plus command hooks bound
// to session-lifecycle moments (see docs/development/f5-experience.md). The
// adapter ignores the block; the extension runs the hooks.

// Declaration order is execution order within a session, which the commands
// rely on when running several hooks in sequence.
export const HOOK_NAMES = ["beforeSession", "onAttachRequest", "afterConfigurationDone", "afterSessionEnd"] as const;
export type HookName = (typeof HOOK_NAMES)[number];

export type TargetHooks = Partial<Record<HookName, string[]>>;

export interface Target {
    host?: string;
    hooks?: TargetHooks;
}

export function getTarget(configuration: Record<string, unknown>): Target | undefined {
    const target = configuration.target;
    return target && typeof target === "object" ? (target as Target) : undefined;
}

export function hookCommands(target: Target, hook: HookName): string[] {
    const commands = target.hooks?.[hook];
    return Array.isArray(commands) ? commands.filter((c) => typeof c === "string" && c.trim()) : [];
}

// The hooks that actually have commands, in execution order.
export function populatedHooks(target: Target): HookName[] {
    return HOOK_NAMES.filter((hook) => hookCommands(target, hook).length > 0);
}

// Complaints about a target block that would otherwise be ignored in silence:
// a stale key from an older configuration, or a misspelled hook name. Doing
// nothing without saying so is the worst failure mode this feature has.
export function validateTarget(target: Target): string[] {
    const problems: string[] = [];
    const unknownKeys = Object.keys(target).filter((key) => key !== "host" && key !== "hooks");
    if (unknownKeys.length > 0) {
        problems.push(
            `'target' has no '${unknownKeys.join("', '")}' - it takes 'host' and 'hooks' ` +
                `(${HOOK_NAMES.join(", ")}), each hook a list of command lines.`
        );
    }
    const hooks = target.hooks;
    if (hooks && typeof hooks === "object") {
        const unknownHooks = Object.keys(hooks).filter((hook) => !HOOK_NAMES.includes(hook as HookName));
        if (unknownHooks.length > 0) {
            problems.push(`'target.hooks' has no '${unknownHooks.join("', '")}' - use ${HOOK_NAMES.join(", ")}.`);
        }
    }
    return problems;
}
