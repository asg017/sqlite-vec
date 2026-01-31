/**
 * Returns the full path to the sqlite-vec loadable extension bundled with this package
 */
export declare function getLoadablePath(): string;

interface Db {
    loadExtension(file: string, entrypoint?: string | undefined): void;
}

/**
 * Load the sqlite-vec extension into a SQLite database connection
 */
export declare function load(db: Db): void;
