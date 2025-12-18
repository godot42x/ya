import * as vscode from 'vscode';
import * as path from 'path';
import * as child_process from 'child_process';
import * as fs from 'fs';
import { TestController } from './testController';

// Global variables
let outputChannel: vscode.OutputChannel;
let diagnosticCollection: vscode.DiagnosticCollection;
let testResults: Map<string, TestResultInfo> = new Map();
let testController: TestController;

// Test result structure
interface TestResultInfo {
  passed: boolean;
  elapsedMs: number;
  errorMessage?: string;
  timestamp: number;
}

// Build system detection
enum BuildSystem {
  XMake = 'xmake',
  CMake = 'cmake',
  Unknown = 'unknown'
}

// Function to get project root
function getProjectRoot(): string {
  const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
  if (!workspaceFolder) {
    throw new Error('No workspace folder found');
  }
  return workspaceFolder.uri.fsPath;
}

// Detect build system
function detectBuildSystem(): BuildSystem {
  const projectRoot = getProjectRoot();
  const config = vscode.workspace.getConfiguration('ya.test');
  const buildSystem = config.get<string>('buildSystem', 'auto');
  
  if (buildSystem !== 'auto') {
    return buildSystem as BuildSystem;
  }
  
  // Auto-detect
  if (fs.existsSync(path.join(projectRoot, 'xmake.lua'))) {
    return BuildSystem.XMake;
  }
  if (fs.existsSync(path.join(projectRoot, 'CMakeLists.txt'))) {
    return BuildSystem.CMake;
  }
  
  return BuildSystem.Unknown;
}

// Get executable extension based on platform
function getExecutableExtension(): string {
  return process.platform === 'win32' ? '.exe' : '';
}

// Function to get test runner path
function getTestRunnerPath(): string {
  const config = vscode.workspace.getConfiguration('ya.test');
  const customPath = config.get<string>('testRunnerPath');
  
  if (customPath && customPath !== '${ya.test.buildDir}/test_runner.exe') {
    // Replace variables
    const projectRoot = getProjectRoot();
    return customPath
      .replace('${workspaceFolder}', projectRoot)
      .replace('${ya.test.buildMode}', config.get<string>('buildMode', 'debug'));
  }
  
  const buildMode = config.get<string>('buildMode', 'debug');
  const projectRoot = getProjectRoot();
  const buildSystem = detectBuildSystem();
  const exeExt = getExecutableExtension();
  
  // Construct path based on build system
  if (buildSystem === BuildSystem.XMake) {
    if (process.platform === 'win32') {
      return path.join(projectRoot, 'build', 'windows', 'x64', buildMode, `test-runner${exeExt}`);
    } else if (process.platform === 'linux') {
      return path.join(projectRoot, 'build', 'linux', 'x86_64', buildMode, `test-runner${exeExt}`);
    } else if (process.platform === 'darwin') {
      return path.join(projectRoot, 'build', 'macosx', 'x86_64', buildMode, `test-runner${exeExt}`);
    }
  } else if (buildSystem === BuildSystem.CMake) {
    return path.join(projectRoot, 'build', 'bin', `test-runner${exeExt}`);
  }
  
  // Fallback
  return path.join(projectRoot, 'build', `test-runner${exeExt}`);
}

// Function to build tests
function buildTests(): Promise<boolean> {
  return new Promise((resolve) => {
    const projectRoot = getProjectRoot();
    const buildSystem = detectBuildSystem();
    
    outputChannel.appendLine('Building tests...');
    outputChannel.appendLine(`Detected build system: ${buildSystem}`);
    
    let buildCommand: string;
    let buildArgs: string[];
    
    if (buildSystem === BuildSystem.XMake) {
      buildCommand = 'xmake';
      buildArgs = ['build', 'test-runner'];
    } else if (buildSystem === BuildSystem.CMake) {
      buildCommand = 'cmake';
      buildArgs = ['--build', 'build', '--target', 'test-runner'];
    } else {
      outputChannel.appendLine('❌ Unknown build system. Please configure manually.');
      vscode.window.showErrorMessage('Unknown build system. Install XMake or CMake, or configure build system manually.');
      resolve(false);
      return;
    }

    const buildProcess = child_process.spawn(buildCommand, buildArgs, {
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
      } else {
        outputChannel.appendLine('❌ Build failed');
        vscode.window.showErrorMessage('Build failed. Check output for details.');
        resolve(false);
      }
    });
  });
}

// Parse test output and extract results
function parseTestOutput(output: string, testName: string): TestResultInfo | null {
  // Look for patterns like: "Running test: TestName... PASSED (12.5ms)"
  const passedRegex = new RegExp(`Running test: ${testName}\\.\\.\\. PASSED \\((\\d+\\.?\\d*)ms\\)`);
  const failedRegex = new RegExp(`Running test: ${testName}\\.\\.\\. FAILED(?:\\s+-\\s+(.+))?`);
  
  const passedMatch = output.match(passedRegex);
  if (passedMatch) {
    return {
      passed: true,
      elapsedMs: parseFloat(passedMatch[1]),
      timestamp: Date.now()
    };
  }
  
  const failedMatch = output.match(failedRegex);
  if (failedMatch) {
    return {
      passed: false,
      elapsedMs: 0,
      errorMessage: failedMatch[1] || 'Test failed',
      timestamp: Date.now()
    };
  }
  
  return null;
}

// Update diagnostics for failed tests
function updateDiagnostics(document: vscode.TextDocument, testName: string, result: TestResultInfo) {
  const text = document.getText();
  const regex = new RegExp(`TEST_CASE\\s*\\(\\s*${testName}\\s*\\)`, 'g');
  const match = regex.exec(text);
  
  if (match && !result.passed) {
    const position = document.positionAt(match.index);
    const range = new vscode.Range(position, position.translate(1, 0));
    
    const diagnostic = new vscode.Diagnostic(
      range,
      result.errorMessage || 'Test failed',
      vscode.DiagnosticSeverity.Error
    );
    diagnostic.source = 'ya-test';
    
    const existingDiagnostics = diagnosticCollection.get(document.uri) || [];
    diagnosticCollection.set(document.uri, [...existingDiagnostics, diagnostic]);
  }
}

// Function to run tests
function runTests(testName?: string): Promise<void> {
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

      // Clear diagnostics for this test
      if (testName) {
        diagnosticCollection.clear();
      }

      const projectRoot = getProjectRoot();
      const args = testName ? [testName] : [];
      
      const testProcess = child_process.spawn(testRunnerPath, args, {
        cwd: projectRoot,
        shell: true
      });

      let outputBuffer = '';

      testProcess.stdout?.on('data', (data) => {
        const text = data.toString();
        outputBuffer += text;
        outputChannel.append(text);
      });

      testProcess.stderr?.on('data', (data) => {
        const text = data.toString();
        outputBuffer += text;
        outputChannel.append(text);
      });

      testProcess.on('close', (code) => {
        outputChannel.appendLine('='.repeat(50));
        
        // Parse results and update cache
        if (testName) {
          const result = parseTestOutput(outputBuffer, testName);
          if (result) {
            testResults.set(testName, result);
            
            // Update diagnostics for active editor
            const activeEditor = vscode.window.activeTextEditor;
            if (activeEditor) {
              updateDiagnostics(activeEditor.document, testName, result);
            }
            
            if (result.passed) {
              outputChannel.appendLine(`✅ Test passed in ${result.elapsedMs.toFixed(2)}ms`);
              vscode.window.showInformationMessage(`Test ${testName} passed (${result.elapsedMs.toFixed(2)}ms)`);
            } else {
              outputChannel.appendLine(`❌ Test failed: ${result.errorMessage}`);
              vscode.window.showWarningMessage(`Test ${testName} failed`);
            }
          }
        } else {
          if (code === 0) {
            outputChannel.appendLine('✅ All tests passed!');
            vscode.window.showInformationMessage('All tests passed!');
          } else {
            outputChannel.appendLine('❌ Some tests failed.');
            vscode.window.showWarningMessage('Some tests failed. Check output for details.');
          }
        }
        
        // Trigger CodeLens refresh
        vscode.commands.executeCommand('_workbench.action.reloadWindow');
        
        resolve();
      });

    } catch (error) {
      outputChannel.appendLine(`❌ Error: ${error}`);
      vscode.window.showErrorMessage(`Failed to run tests: ${error}`);
      resolve();
    }
  });
}

export function activate(context: vscode.ExtensionContext) {
  console.log('🚀 ya Test Runner extension activated');
  
  // Create output channel
  outputChannel = vscode.window.createOutputChannel('ya Test Runner');
  context.subscriptions.push(outputChannel);
  
  // Create diagnostic collection
  diagnosticCollection = vscode.languages.createDiagnosticCollection('ya-test');
  context.subscriptions.push(diagnosticCollection);
  
  // Initialize Test Explorer
  try {
    const testRunnerPath = getTestRunnerPath();
    const projectRoot = getProjectRoot();
    testController = new TestController(context, testRunnerPath, projectRoot);
  } catch (error) {
    console.warn('Failed to initialize Test Controller:', error);
  }
  
  // Register CodeLens provider for test cases
  const codeLensProvider = {
    provideCodeLenses(document: vscode.TextDocument): vscode.CodeLens[] {
      const lenses: vscode.CodeLens[] = [];
      const text = document.getText();
      
      // Find all TEST_CASE macros (support multi-line declarations)
      const regex = /TEST_CASE\s*\(\s*(\w+)\s*\)/g;
      let match;
      
      while ((match = regex.exec(text)) !== null) {
        const testName = match[1];
        const position = document.positionAt(match.index);
        const range = new vscode.Range(position, position.translate(0, match[0].length));
        
        // Get cached result if available
        const result = testResults.get(testName);
        let title = '▶️ Run Test';
        
        if (result) {
          const age = Date.now() - result.timestamp;
          const isRecent = age < 5 * 60 * 1000; // 5 minutes
          
          if (isRecent) {
            if (result.passed) {
              title = `✅ Run Test (${result.elapsedMs.toFixed(1)}ms)`;
            } else {
              title = `❌ Run Test (failed)`;
            }
          }
        }
        
        lenses.push(new vscode.CodeLens(range, {
          title: title,
          command: 'ya.runTest',
          arguments: [testName]
        }));
        
        // Add debug button
        lenses.push(new vscode.CodeLens(range, {
          title: '🐛 Debug Test',
          command: 'ya.debugTest',
          arguments: [testName]
        }));
      }
      
      // Add "Run All Tests" lens at the top if any tests found
      if (lenses.length > 0) {
        const topRange = new vscode.Range(0, 0, 0, 0);
        const testCount = lenses.length / 2; // Divided by 2 because we have run + debug buttons
        lenses.unshift(new vscode.CodeLens(topRange, {
          title: `🧪 Run All Tests (${testCount} found)`,
          command: 'ya.runAllTests'
        }));
      }
      
      return lenses;
    }
  };
  
  // Register the CodeLens provider
  context.subscriptions.push(
    vscode.languages.registerCodeLensProvider(
      { language: 'cpp', scheme: 'file' }, 
      codeLensProvider
    )
  );
  
  // Command to run a specific test
  context.subscriptions.push(
    vscode.commands.registerCommand('ya.runTest', async (testName: string) => {
      outputChannel.show();
      await runTests(testName);
    })
  );
  
  // Command to run all tests
  context.subscriptions.push(
    vscode.commands.registerCommand('ya.runAllTests', async () => {
      outputChannel.show();
      await runTests();
    })
  );
  
  // Command to build tests
  context.subscriptions.push(
    vscode.commands.registerCommand('ya.buildTests', async () => {
      outputChannel.show();
      await buildTests();
    })
  );
  
  // Command to debug a specific test
  context.subscriptions.push(
    vscode.commands.registerCommand('ya.debugTest', async (testName: string) => {
      const testRunnerPath = getTestRunnerPath();
      
      // Check if test runner exists
      if (!fs.existsSync(testRunnerPath)) {
        const buildSuccess = await buildTests();
        if (!buildSuccess) {
          vscode.window.showErrorMessage('Build failed. Cannot debug test.');
          return;
        }
      }
      
      // Start debugging session
      const debugConfig: vscode.DebugConfiguration = {
        name: `Debug Test: ${testName}`,
        type: 'cppdbg',
        request: 'launch',
        program: testRunnerPath,
        args: [testName],
        cwd: getProjectRoot(),
        stopAtEntry: false,
        externalConsole: false,
        MIMode: process.platform === 'win32' ? 'gdb' : 'gdb',
      };
      
      await vscode.debug.startDebugging(undefined, debugConfig);
    })
  );
  
  // Clear diagnostics when document is closed
  context.subscriptions.push(
    vscode.workspace.onDidCloseTextDocument(doc => {
      diagnosticCollection.delete(doc.uri);
    })
  );
}

export function deactivate() {
  console.log('👋 ya Test Runner extension deactivated');
}
