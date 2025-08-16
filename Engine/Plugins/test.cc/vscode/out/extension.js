"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.deactivate = exports.activate = void 0;
const vscode = require("vscode");
const path = require("path");
const child_process = require("child_process");
const fs = require("fs");
// Global variables
let outputChannel;
// Function to get project root
function getProjectRoot() {
    const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
    if (!workspaceFolder) {
        throw new Error('No workspace folder found');
    }
    return workspaceFolder.uri.fsPath;
}
// Function to get test runner path
function getTestRunnerPath() {
    const config = vscode.workspace.getConfiguration('ya.test');
    const buildMode = config.get('buildMode', 'debug');
    const projectRoot = getProjectRoot();
    return path.join(projectRoot, 'build', 'windows', 'x64', buildMode, 'test_runner.exe');
}
// Function to build tests
function buildTests() {
    return new Promise((resolve) => {
        const projectRoot = getProjectRoot();
        outputChannel.appendLine('Building tests...');
        const buildProcess = child_process.spawn('xmake', ['build', 'test_runner'], {
            cwd: projectRoot,
            shell: true
        });
        buildProcess.stdout?.on('data', (data) => {
            outputChannel.append(data.toString());
        });
        buildProcess.stderr?.on('data', (data) => {
            outputChannel.append(data.toString());
        });
        buildProcess.on('close', (code) => {
            if (code === 0) {
                outputChannel.appendLine('✅ Tests built successfully');
                vscode.window.showInformationMessage('Tests built successfully');
                resolve(true);
            }
            else {
                outputChannel.appendLine('❌ Build failed');
                vscode.window.showErrorMessage('Build failed. Check output for details.');
                resolve(false);
            }
        });
    });
}
// Function to run tests
function runTests(testName) {
    return new Promise(async (resolve) => {
        try {
            const testRunnerPath = getTestRunnerPath();
            // Check if test runner exists
            if (!fs.existsSync(testRunnerPath)) {
                outputChannel.appendLine('Test runner not found. Building tests...');
                const buildSuccess = await buildTests();
                if (!buildSuccess) {
                    outputChannel.appendLine('Build failed. Cannot run tests.');
                    resolve();
                    return;
                }
            }
            const testTitle = testName ? `Test: ${testName}` : 'All Tests';
            outputChannel.clear();
            outputChannel.appendLine(`🚀 Running ${testTitle}`);
            outputChannel.appendLine(`📍 Test runner: ${testRunnerPath}`);
            outputChannel.appendLine('='.repeat(50));
            const projectRoot = getProjectRoot();
            const args = testName ? [testName] : [];
            const testProcess = child_process.spawn(testRunnerPath, args, {
                cwd: projectRoot,
                shell: true
            });
            testProcess.stdout?.on('data', (data) => {
                outputChannel.append(data.toString());
            });
            testProcess.stderr?.on('data', (data) => {
                outputChannel.append(data.toString());
            });
            testProcess.on('close', (code) => {
                outputChannel.appendLine('='.repeat(50));
                if (code === 0) {
                    outputChannel.appendLine('✅ All tests passed!');
                    vscode.window.showInformationMessage(testName ? `Test ${testName} passed!` : 'All tests passed!');
                }
                else {
                    outputChannel.appendLine('❌ Some tests failed.');
                    vscode.window.showWarningMessage(testName ? `Test ${testName} failed.` : 'Some tests failed. Check output for details.');
                }
                resolve();
            });
        }
        catch (error) {
            outputChannel.appendLine(`❌ Error: ${error}`);
            vscode.window.showErrorMessage(`Failed to run tests: ${error}`);
            resolve();
        }
    });
}
function activate(context) {
    console.log('🚀 ya Test Runner extension activated');
    // Create output channel
    outputChannel = vscode.window.createOutputChannel('ya Test Runner');
    context.subscriptions.push(outputChannel);
    // Register CodeLens provider for test cases
    const codeLensProvider = {
        provideCodeLenses(document) {
            const lenses = [];
            const text = document.getText();
            // Find all TEST_CASE macros
            const regex = /TEST_CASE\s*\(\s*(\w+)\s*\)/g;
            let match;
            while ((match = regex.exec(text)) !== null) {
                const testName = match[1];
                const position = document.positionAt(match.index);
                const range = new vscode.Range(position, position.translate(0, match[0].length));
                lenses.push(new vscode.CodeLens(range, {
                    title: '▶️ Run Test',
                    command: 'ya.runTest',
                    arguments: [testName]
                }));
            }
            // Add "Run All Tests" lens at the top if any tests found
            if (lenses.length > 0) {
                const topRange = new vscode.Range(0, 0, 0, 0);
                lenses.unshift(new vscode.CodeLens(topRange, {
                    title: `🧪 Run All Tests (${lenses.length} found)`,
                    command: 'ya.runAllTests'
                }));
            }
            return lenses;
        }
    };
    // Register the CodeLens provider
    context.subscriptions.push(vscode.languages.registerCodeLensProvider({ language: 'cpp', scheme: 'file' }, codeLensProvider));
    // Command to run a specific test
    context.subscriptions.push(vscode.commands.registerCommand('ya.runTest', async (testName) => {
        outputChannel.show();
        await runTests(testName);
    }));
    // Command to run all tests
    context.subscriptions.push(vscode.commands.registerCommand('ya.runAllTests', async () => {
        outputChannel.show();
        await runTests();
    }));
    // Command to build tests
    context.subscriptions.push(vscode.commands.registerCommand('ya.buildTests', async () => {
        outputChannel.show();
        await buildTests();
    }));
}
exports.activate = activate;
function deactivate() {
    console.log('👋 ya Test Runner extension deactivated');
}
exports.deactivate = deactivate;
//# sourceMappingURL=extension.js.map