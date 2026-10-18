/** Qt 6.9.3 QJSEngine API v1. No DOM, Node.js, fetch or async function support. */
export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };
export type Values = { [key: string]: Json };
export interface HostError { code: "INVALID_ARGUMENT" | "PERMISSION_DENIED" | "NOT_FOUND" | "CONFLICT" | "EDITOR_DIRTY" | "SAVE_FAILED" | "NETWORK_ERROR" | "TIMEOUT" | "CANCELED" | "RATE_LIMITED" | "PLUGIN_STOPPED"; message: string; requestId?: string; }
export type Callback<T> = (error: HostError | null, result: T) => void;
export interface Disposable { dispose(): void; }
export interface Handle extends Disposable { cancel(): void; }
export interface UiNode {
    type: "column" | "row" | "group" | "scroll" | "text" | "icon" | "button" | "separator" | "progress" | "input" | "multiline" | "toggle" | "select" | "number" | "list";
    id?: string; text?: string; label?: string; description?: string; action?: string;
    value?: Json; enabled?: boolean; visible?: boolean; children?: UiNode[];
    required?: boolean; min?: number; max?: number; maxLength?: number; placeholder?: string;
    items?: (string | { label: string; value: Json })[];
}
export interface ViewDefinition { name?: string; tree: UiNode; }
export interface ComponentDefinition extends ViewDefinition { settings?: ViewDefinition; previewText?: string; displayPolicy?: "expandedOnly" | "whenAvailable"; }
export interface CourseSnapshot {
    profileSession: string; profileRevision: string; tableRevision: string; date: string; name: string; valid: boolean; error: string;
    classes: { subject: string; startTime: string; endTime: string }[];
    phase: number; currentClassIndex: number; nextClassIndex: number; remainingSeconds: number; countdownTarget: string;
}
export interface ProfileSnapshot { session: string; revision: string; profile: Values; canceledDates?: string[]; }
export interface ProfileCommit { session: string; revision: string; canceledDates: string[]; }
export interface ProfileOperation {
    op: "profile.updateInfo" | "subject.add" | "subject.update" | "subject.remove" | "timeLine.add" | "timeLine.update" | "timeLine.remove" | "week.add" | "week.update" | "week.remove" | "day.set" | "class.add" | "class.update" | "class.remove" | "date.reschedule" | "date.restore" | "date.swap";
    ref?: string; value?: Values; weekId?: string; weekday?: number; date?: string;
    firstDate?: string; firstWeekId?: string; first?: number; secondDate?: string; secondWeekId?: string; second?: number;
}
export interface Context {
    courses: { getSnapshot(callback: Callback<CourseSnapshot>): Handle };
    events: { on(name: "course.started" | "course.ended" | "course.startingSoon" | "courses.tableChanged" | "courses.stateChanged" | "profile.changed" | "theme.changed" | "ui.created" | "ui.destroyed" | "ui.action" | "settings.saved", handler: (event: any) => void): Disposable };
    timers: { after(ms: number, handler: () => void): Handle; every(ms: number, handler: () => void): Handle };
    storage: { get(key: string, callback: Callback<Json | undefined>): Handle; set(key: string, value: Json, callback?: Callback<boolean>): Handle; remove(key: string, callback?: Callback<boolean>): Handle };
    http: { request(options: { url: string; method?: "GET" | "POST" | "PUT" | "PATCH" | "DELETE" | "HEAD"; headers?: Record<string, string>; body?: string | Values | Json[]; timeoutMs?: number }, callback: Callback<{ status: number; headers: Record<string, string>; body: string }>): Handle };
    profiles: { getCurrent(callback: Callback<ProfileSnapshot>): Handle; applyChanges(request: { session: string; expectedRevision: string; operations: ProfileOperation[] }, callback: Callback<ProfileCommit>): Handle };
    settings: { get(callback: Callback<{ revision: string; themeColor: string; themeMode: number; courseBarEnabled: boolean; courseBarHeight: number; componentSpacing: number }>): Handle; update(request: { expectedRevision: string; changes: { themeColor?: string; themeMode?: number; courseBarEnabled?: boolean; courseBarHeight?: number; componentSpacing?: number } }, callback: Callback<{ revision: string }>): Handle };
    ui: { registerSettingsPage(id: string, definition: ViewDefinition): Handle; registerComponent(id: string, definition: ComponentDefinition): Handle; registerWindow(id: string, definition: ViewDefinition): Handle; update(instanceId: string, patch: Record<string, { text?: string; value?: Json; enabled?: boolean; visible?: boolean }>): Handle; setTree(instanceId: string, tree: UiNode): Handle; openWindow(id: string): Handle };
    notifications: { show(options: { title: string; message: string }): Handle };
    log: { debug(message: string): void; info(message: string): void; warn(message: string): void; error(message: string): void };
}
export declare function activate(ctx: Context): void;
export declare function deactivate(ctx: Context): void;
