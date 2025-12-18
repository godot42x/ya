"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.TestController = void 0;
const vscode = require("vscode");
const child_process = require("child_process");
const fs = require("fs");
class TestController {
    constructor(context, testRunnerPath, projectRoot) {
        this.testRunnerPath = testRunnerPath;
        this.projectRoot = projectRoot;
        // Create test controller
        this.controller = vscode.tests.createTestController('yaTestController', 'ya Test Framework');
        context.subscriptions.push(this.controller);
        // Set up run handler
        this.controller.createRunProfile('Run Tests', vscode.TestRunProfileKind.Run, (request, token) => this.runTests(request, token), true);
        // Set up debug handler
        this.controller.createRunProfile('Debug Tests', vscode.TestRunProfileKind.Debug, (request, token) => this.debugTests(request, token), false);
        // Discover tests on activation
        this.discoverTests();
        // Watch for file changes
        const watcher = vscode.workspace.createFileSystemWatcher('**/*.cpp');
        watcher.onDidChange(() => this.discoverTests());
        watcher.onDidCreate(() => this.discoverTests());
        watcher.onDidDelete(() => this.discoverTests());
        context.subscriptions.push(watcher);
    }
    async discoverTests() {
        // Clear existing tests
        this.controller.items.replace([]);
        // Scan workspace for TEST_CASE macros
        const files = await vscode.workspace.findFiles('**/*.cpp', '**/node_modules/**');
        for (const file of files) {
            const document = await vscode.workspace.openTextDocument(file);
            const text = document.getText();
            const regex = /TEST_CASE\s*\(\s*(\w+)\s*\)/g;
            let match;
            while ((match = regex.exec(text)) !== null) {
                const testName = match[1];
                const position = document.positionAt(match.index);
                const range = new vscode.Range(position, position.translate(1, 0));
                // Create test item
                const testItem = this.controller.createTestItem(testName, testName, file);
                testItem.range = range;
                this.controller.items.add(testItem);
            }
        }
    }
    async runTests(request, token) {
        const run = this.controller.createTestRun(request);
        // Determine which tests to run
        const tests = request.include ? Array.from(request.include) : Array.from(this.controller.items);
        for (const test of tests) {
            if (token.isCancellationRequested) {
                run.skipped(test);
                continue;
            }
            run.started(test);
            try {
                const result = await this.runSingleTest(test.id);
                if (result.passed) {
                    run.passed(test, result.elapsedMs);
                }
                else {
                    const message = vscode.TestMessage.diff(result.errorMessage || 'Test failed', '', '');
                    if (test.range) {
                        message.location = new vscode.Location(test.uri, test.range);
                    }
                    run.failed(test, message, result.elapsedMs);
                }
            }
            catch (error) {
                const message = new vscode.TestMessage(`Error running test: ${error}`);
                run.errored(test, message);
            }
        }
        run.end();
    }
    async debugTests(request, token) {
        const tests = request.include ? Array.from(request.include) : Array.from(this.controller.items);
        if (tests.length === 0) {
            return;
        }
        // For now, debug the first test
        const test = tests[0];
        const debugConfig = {
            name: `Debug Test: ${test.id}`,
            type: 'cppdbg',
            request: 'launch',
            program: this.testRunnerPath,
            args: [test.id],
            cwd: this.projectRoot,
            stopAtEntry: false,
            externalConsole: false,
            MIMode: process.platform === 'win32' ? 'gdb' : 'gdb',
        };
        await vscode.debug.startDebugging(undefined, debugConfig);
    }
    runSingleTest(testName) {
        return new Promise((resolve, reject) => {
            if (!fs.existsSync(this.testRunnerPath)) {
                reject(new Error('Test runner not found'));
                return;
            }
            const testProcess = child_process.spawn(this.testRunnerPath, [testName], {
                cwd: this.projectRoot,
                shell: true
            });
            let output = '';
            testProcess.stdout?.on('data', (data) => {
                output += data.toString();
            });
            testProcess.stderr?.on('data', (data) => {
                output += data.toString();
            });
            testProcess.on('close', (code) => {
                // Parse output
                const passedRegex = new RegExp(`Running test: ${testName}\\.\\.\\. PASSED \\((\\d+\\.?\\d*)ms\\)`);
                const failedRegex = new RegExp(`Running test: ${testName}\\.\\.\\. FAILED(?:\\s+-\\s+(.+))?`);
                const passedMatch = output.match(passedRegex);
                if (passedMatch) {
                    resolve({
                        passed: true,
                        elapsedMs: parseFloat(passedMatch[1])
                    });
                    return;
                }
                const failedMatch = output.match(failedRegex);
                if (failedMatch) {
                    resolve({
                        passed: false,
                        elapsedMs: 0,
                        errorMessage: failedMatch[1] || 'Test failed'
                    });
                    return;
                }
                // Fallback
                resolve({
                    passed: code === 0,
                    elapsedMs: 0,
                    errorMessage: code !== 0 ? 'Test failed' : undefined
                });
            });
            testProcess.on('error', (error) => {
                reject(error);
            });
        });
    }
}
exports.TestController = TestController;
//# sourceMappingURL=testController.js.map