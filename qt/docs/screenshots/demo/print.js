// xournal-qt: prints the demo's HTML pages as PDFs (make-demo.sh). node print.js <in.html> <out.pdf>
const { chromium } = require("playwright");
(async () => {
  const [inFile, outFile] = process.argv.slice(2);
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto("file://" + require("path").resolve(inFile));
  await page.pdf({ path: outFile, preferCSSPageSize: true, printBackground: true });
  await browser.close();
})();
