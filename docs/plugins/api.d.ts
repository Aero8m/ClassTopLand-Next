/** Plugin API v2. Qt 6.9.3 QJSEngine, callback-based; no DOM/Node/fetch/async function. */
export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };
export type Values = { [key: string]: Json };
export interface HostError { code: string; message: string; requestId?: string; }
export type Callback<T> = (error: HostError | null, result: T) => void;
export interface Disposable { dispose(): void; }
export interface Handle extends Disposable { cancel(): void; }
export interface ConfigSnapshot { exists: boolean; value?: Json; revision: string; }
export interface ConfigStore {
    read(callback: Callback<ConfigSnapshot>): Handle;
    commit(expectedRevision: string, value: Json, callback: Callback<{ revision: string }>): Handle;
}
export interface View {
    readonly instanceId: string;
    readonly kind: "page" | "component" | "componentSettings";
    readonly contributionId: string;
    readonly componentInstance: string;
    readonly initialSettings: Values;
    readonly config: ConfigStore;
    get(object: string, property: string, callback: Callback<Json>): Handle;
    set(object: string, properties: Values, callback?: Callback<boolean>): Handle;
    on(object: string, signal: string, handler: (...args: Json[]) => void): Disposable;
    create(parent: string, className: string, objectName: string, callback: Callback<string>): Handle;
    remove(object: string, callback?: Callback<boolean>): Handle;
    insert(layout: string, child: string, options: { index?: number; stretch?: number; row?: number; column?: number }, callback?: Callback<boolean>): Handle;
    invoke(object: string, operation: "click" | "setItems" | "addPage" | "select", args: Values, callback?: Callback<Json>): Handle;
    setDirty(dirty: boolean, callback?: Callback<boolean>): Handle;
    setAvailable(available: boolean, callback?: Callback<boolean>): Handle;
    save(callback?: (error: HostError | null) => void): void;
    discard(callback?: (error: HostError | null) => void): void;
}
export interface PageController {
    show?(): void;
    hide?(): void;
    destroy?(): void;
    save?(done: (error?: HostError | null) => void): void;
    discard?(done: (error?: HostError | null) => void): void;
}
export type PageFactory = (view: View) => PageController;
export interface ActionCall {
    readonly requestId: string;
    readonly origin: "agent" | "plugin";
    isCanceled(): boolean;
    onCancel(handler: () => void): void;
}
export type ActionHandler = (args: Json, done: Callback<Json>, call: ActionCall) => void;
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
    readonly apiVersion: 2;
    courses: { getSnapshot(callback: Callback<CourseSnapshot>): Handle };
    events: { on(name: "course.started" | "course.ended" | "course.startingSoon" | "courses.tableChanged" | "courses.stateChanged" | "profile.changed" | "theme.changed", handler: (event: any) => void): Disposable };
    timers: { after(ms: number, handler: () => void): Handle; every(ms: number, handler: () => void): Handle };
    storage: { get(key: string, callback: Callback<Json | undefined>): Handle; set(key: string, value: Json, callback?: Callback<boolean>): Handle; remove(key: string, callback?: Callback<boolean>): Handle };
    config: { read(key: string, callback: Callback<ConfigSnapshot>): Handle; commit(key: string, expectedRevision: string, value: Json, callback: Callback<{ revision: string }>): Handle };
    http: { request(options: { url: string; method?: "GET" | "POST" | "PUT" | "PATCH" | "DELETE" | "HEAD"; headers?: Record<string, string>; body?: string | Values | Json[]; timeoutMs?: number }, callback: Callback<{ status: number; headers: Record<string, string>; body: string }>): Handle };
    profiles: { getCurrent(callback: Callback<ProfileSnapshot>): Handle; applyChanges(request: { session: string; expectedRevision: string; operations: ProfileOperation[] }, callback: Callback<ProfileCommit>): Handle };
    settings: { get(callback: Callback<{ revision: string; themeColor: string; themeMode: number; courseBarEnabled: boolean; courseBarHeight: number; componentSpacing: number }>): Handle; update(request: { expectedRevision: string; changes: { themeColor?: string; themeMode?: number; courseBarEnabled?: boolean; courseBarHeight?: number; componentSpacing?: number } }, callback: Callback<{ revision: string }>): Handle };
    ui: { registerPage(id: string, factory: PageFactory): Handle; registerComponent(id: string, factories: { create: PageFactory; createSettings?: PageFactory }): Handle; openPage(id: string, callback?: Callback<void>): Handle; catalog(callback: Callback<Record<string, { kind: "widget" | "layout"; properties?: string[]; signals?: string[]; operations?: string[] }>>): Handle };
    actions: { register(id: string, handler: ActionHandler): Handle; invoke(id: string, args: Json, callback: Callback<Json>): Handle };
    notifications: { show(options: { title: string; message: string }, callback?: Callback<void>): Handle };
    log: { debug(message: string): void; info(message: string): void; warn(message: string): void; error(message: string): void };
}
export declare function activate(ctx: Context): void;
export declare function deactivate(ctx: Context): void;
