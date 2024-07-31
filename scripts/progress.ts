const src = Deno.readTextFileSync("sqlite-vec.c");

function numOccuranges(rg) {
  return [...src.matchAll(rg)].length;
}
const numAsserts = numOccuranges(/todo_assert/g);
const numComments = numOccuranges(/TODO/g);
const numHandles = numOccuranges(/todo\(/g);

const realTodos = numOccuranges(/TODO\(/g);

const numTotal = numAsserts + numComments + numHandles - realTodos;

console.log("Number of todo_assert()'s:     ", numAsserts);
console.log('Number of "// TODO" comments:  ', numComments);
console.log("Number of todo panics:         ", numHandles);
console.log("Total TODOs:                   ", numTotal);

console.log();

const TOTAL = 246; //  as of e5b0f4c0c5 (2024-04-20)
const progress = (TOTAL - numTotal) / TOTAL;
const width = 60;

console.log(
  "▓".repeat((progress < 0 ? 0 : progress) * width) +
    "░".repeat((1 - progress) * width) +
    ` (${TOTAL - numTotal}/${TOTAL})`,
);
console.log();
console.log(
  `${(progress * 100.0).toPrecision(2)}% complete to sqlite-vec v0.1.0`,
);
