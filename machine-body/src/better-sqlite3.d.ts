declare module "better-sqlite3" {
  type BindParameters = Record<string, unknown> | unknown[] | undefined;

  interface Statement<Result = unknown> {
    run(params?: BindParameters): Result;
    all(params?: BindParameters): Result[];
  }

  interface DatabaseInstance {
    pragma(source: string): unknown;
    exec(source: string): this;
    prepare<Result = unknown>(source: string): Statement<Result>;
  }

  interface DatabaseConstructor {
    new (filename: string): DatabaseInstance;
  }

  const Database: DatabaseConstructor;

  namespace Database {
    export type Database = DatabaseInstance;
    export type Statement<Result = unknown> = import("better-sqlite3").Statement<Result>;
  }

  export default Database;
}
