import Database from "better-sqlite3";
import { load } from "js-yaml";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";
import { readFileSync, writeFileSync } from "node:fs";
import * as v from "valibot";
import { table } from "table";

const HEADER = `---
outline: 2
---

# API Reference

A complete reference to all the SQL scalar functions, table functions, and virtual tables inside \`sqlite-vec\`.

::: warning
sqlite-vec is pre-v1, so expect breaking changes.
:::

[[toc]]

`;

const REF_PATH = resolve(
  dirname(fileURLToPath(import.meta.url)),
  "../reference.yaml",
);
const EXT_PATH = resolve(
  dirname(fileURLToPath(import.meta.url)),
  "../dist/vec0",
);

const DocSchema = v.objectWithRest(
  {
    sections: v.record(
      v.string(),
      v.object({
        title: v.string(),
        desc: v.string(),
      }),
    ),
  },
  v.record(
    v.string(),
    v.object({
      params: v.array(v.string()),
      desc: v.string(),
      example: v.union([v.string(), v.array(v.string())]),
    }),
  ),
);

const tableConfig = {
  border: {
    topBody: `─`,
    topJoin: `┬`,
    topLeft: `┌`,
    topRight: `┐`,

    bottomBody: `─`,
    bottomJoin: `┴`,
    bottomLeft: `└`,
    bottomRight: `┘`,

    bodyLeft: `│`,
    bodyRight: `│`,
    bodyJoin: `│`,

    joinBody: `─`,
    joinLeft: `├`,
    joinRight: `┤`,
    joinJoin: `┼`,
  },
};

function formatSingleValue(value) {
  if (typeof value === "string") {
    const s = `'${value.replace(/'/g, "''")}'`;
    if (s.split("\n").length > 1) {
      return `/*\n${s}\n*/`;
    }
    return `-- ${s}`;
  }
  if (typeof value === "number") return `-- ${value.toString()}`;
  if (value === null) return "-- NULL";
  if (value instanceof Uint8Array) {
    let s = "X'";
    for (const v of value) {
      s += v.toString(16).toUpperCase().padStart(2, "0");
    }
    s += "'";
    return `-- ${s}`;
  }
  if (typeof value === "object" || Array.isArray(value)) {
    return "-- " + JSON.stringify(value, null, 2);
  }
}
function formatValue(value) {
  if (typeof value === "string") return `'${value}'`;
  if (typeof value === "number") return value;
  if (value === null) return "NULL";
  if (value instanceof Uint8Array) {
    let s = "X'";
    for (const v of value) {
      s += v.toString(16).toUpperCase().padStart(2, "0");
    }
    s += "'";
    return s;
  }
  if (typeof value === "object" || Array.isArray(value)) {
    return JSON.stringify(value, null, 2);
  }
}
function tableize(stmt, results) {
  const columnNames = stmt.columns().map((c) => c.name);
  const rows = results.map((row) =>
    row.map((value) => {
      return formatValue(value);
    })
  );
  return table([columnNames, ...rows], tableConfig);
}

function renderExamples(db, name, example) {
  let md = "```sql\n";

  const examples = Array.isArray(example) ? example : [example];
  for (const example of examples) {
    const sql = example
      /* Strip any '```sql'  markdown at the beginning */
      .replace(/^\w*```sql/, "")
      /* Strip any '```'  markdown at the end */
      .replace(/```\w*$/m, "")
      .trim();
    let stmt, results, error;
    results = null;
    try {
      stmt = db.prepare(sql);
      try {
        stmt.raw(true);
      } catch (err) {
        1;
      }
    } catch (error) {
      console.error(`Error preparing statement for ${name}:`);
      console.error(error);
      throw Error();
    }

    try {
      results = stmt.all();
    } catch (e) {
      error = e.message;
    }

    md += sql + "\n";

    if (!results) {
      md += `-- ❌ ${error}\n\n`;
      continue;
    }

    const result = results.length > 1 || stmt.columns().length > 1
      ? `/*\n${tableize(stmt, results)}\n*/\n`
      : formatSingleValue(results[0][0]);
    md += result + "\n\n";
  }

  md += "\n```\n\n";

  return md;
}

let md = HEADER;
const doc = v.parse(DocSchema, load(readFileSync(REF_PATH, "utf8")));

const db = new Database();
db.loadExtension(EXT_PATH);

for (const section in doc.sections) {
  md += `## ${doc.sections[section].title} {#${section}} \n\n`;
  md += doc.sections[section].desc;
  md += "\n\n";

  for (
    const [name, { params, desc, example }] of Object.entries(
      doc[section],
    )
  ) {
    const headerText = `\`${name}(${(params ?? []).join(", ")})\` {#${name}}`;

    md += "### " + headerText + "\n\n";

    md += desc + "\n\n";
    md += renderExamples(db, name, example);
  }
}

writeFileSync("api-reference.md", md, "utf8");
console.log("done");
