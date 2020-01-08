#include "processortab.h"
#include "ui_processortab.h"

#include <QScrollBar>
#include <QSpinBox>

#include "instructionmodel.h"
#include "parser.h"
#include "pipeline.h"
#include "processorregistry.h"
#include "processorselectiondialog.h"
#include "rundialog.h"

#include "VSRTL/graphics/vsrtl_widget.h"

#include "processors/ripesprocessor.h"

ProcessorTab::ProcessorTab(ProcessorHandler& handler, QToolBar* toolbar, QWidget* parent)
    : RipesTab(toolbar, parent), m_handler(handler), m_ui(new Ui::ProcessorTab) {
    m_ui->setupUi(this);

    m_vsrtlWidget = m_ui->vsrtlWidget;

    // Load the default processor
    m_vsrtlWidget->setDesign(m_handler.getProcessor());
    updateInstructionModel();

    setupSimulatorActions();

    // Create temporary pipeline widget to reduce changeset during VSRTL integration
    tmp_pipelineWidget = new PipelineWidget(this);

    // Setup updating signals
    connect(this, &ProcessorTab::update, m_ui->registerContainer, &RegisterContainerWidget::update);
    connect(this, &ProcessorTab::update, tmp_pipelineWidget, &PipelineWidget::update);
    connect(this, &ProcessorTab::update, this, &ProcessorTab::updateMetrics);

    // Connect ECALL functionality to application output log and scroll to bottom
    connect(this, &ProcessorTab::appendToLog, [this](QString string) {
        m_ui->console->insertPlainText(string);
        m_ui->console->verticalScrollBar()->setValue(m_ui->console->verticalScrollBar()->maximum());
    });

    // Make processor view stretch wrt. consoles
    m_ui->pipelinesplitter->setStretchFactor(0, 1);
    m_ui->pipelinesplitter->setStretchFactor(1, 0);

    // Make processor view stretch wrt. right side tabs
    m_ui->viewSplitter->setStretchFactor(0, 1);
    m_ui->viewSplitter->setStretchFactor(1, 0);

    m_ui->consolesTab->removeTab(1);

    // Initially, no file is loaded, disable toolbuttons
    enableSimulatorControls();
}

void ProcessorTab::printToLog(const QString& text) {
    m_ui->console->insertPlainText(text);
    m_ui->console->verticalScrollBar()->setValue(m_ui->console->verticalScrollBar()->maximum());
}

void ProcessorTab::setupSimulatorActions() {
    const QIcon processorIcon = QIcon(":/icons/cpu.svg");
    m_selectProcessorAction = new QAction(processorIcon, "Select processor", this);
    connect(m_selectProcessorAction, &QAction::triggered, this, &ProcessorTab::processorSelection);
    m_toolbar->addAction(m_selectProcessorAction);
    m_toolbar->addSeparator();

    const QIcon resetIcon = QIcon(":/icons/reset.svg");
    m_resetAction = new QAction(resetIcon, "Reset (F4)", this);
    connect(m_resetAction, &QAction::triggered, this, &ProcessorTab::reset);
    m_resetAction->setShortcut(QKeySequence("F4"));
    m_toolbar->addAction(m_resetAction);

    const QIcon reverseIcon = QIcon(":/icons/rewind.svg");
    m_reverseAction = new QAction(reverseIcon, "Rewind (F5)", this);
    connect(m_reverseAction, &QAction::triggered, this, &ProcessorTab::rewind);
    m_reverseAction->setShortcut(QKeySequence("F5"));
    m_toolbar->addAction(m_reverseAction);

    const QIcon clockIcon = QIcon(":/icons/step.svg");
    m_clockAction = new QAction(clockIcon, "Clock (F6)", this);
    connect(m_clockAction, &QAction::triggered, this, &ProcessorTab::clock);
    m_clockAction->setShortcut(QKeySequence("F6"));
    m_toolbar->addAction(m_clockAction);

    QTimer* timer = new QTimer();
    connect(timer, &QTimer::timeout, this, &ProcessorTab::clock);

    const QIcon startAutoClockIcon = QIcon(":/icons/step-clock.svg");
    const QIcon stopAutoTimerIcon = QIcon(":/icons/stop-clock.svg");
    m_autoClockAction = new QAction(startAutoClockIcon, "Auto clock (F7)", this);
    m_autoClockAction->setShortcut(QKeySequence("F7"));
    m_autoClockAction->setCheckable(true);
    connect(m_autoClockAction, &QAction::toggled, [=](bool checked) {
        if (!checked) {
            timer->stop();
            m_autoClockAction->setIcon(startAutoClockIcon);
        } else {
            timer->start();
            m_autoClockAction->setIcon(stopAutoTimerIcon);
        }
    });
    m_autoClockAction->setChecked(false);
    m_toolbar->addAction(m_autoClockAction);

    m_autoClockInterval = new QSpinBox(this);
    m_autoClockInterval->setRange(1, 10000);
    m_autoClockInterval->setSuffix(" ms");
    m_autoClockInterval->setToolTip("Auto clock interval");
    connect(m_autoClockInterval, qOverload<int>(&QSpinBox::valueChanged),
            [timer](int msec) { timer->setInterval(msec); });
    m_autoClockInterval->setValue(100);
    m_toolbar->addWidget(m_autoClockInterval);

    const QIcon runIcon = QIcon(":/icons/run.svg");
    m_runAction = new QAction(runIcon, "Run (F8)", this);
    m_runAction->setShortcut(QKeySequence("F8"));
    connect(m_runAction, &QAction::triggered, this, &ProcessorTab::run);
    m_toolbar->addAction(m_runAction);

    m_toolbar->addSeparator();

    const QIcon tagIcon = QIcon(":/icons/tag.svg");
    m_displayValuesAction = new QAction(tagIcon, "Display signal values", this);
    m_displayValuesAction->setCheckable(true);
    m_displayValuesAction->setChecked(false);
    connect(m_displayValuesAction, &QAction::triggered, m_vsrtlWidget, &vsrtl::VSRTLWidget::setOutputPortValuesVisible);
    m_toolbar->addAction(m_displayValuesAction);

    const QIcon expandIcon = QIcon(":/icons/expand.svg");
    m_fitViewAction = new QAction(expandIcon, "Fit to view", this);
    connect(m_fitViewAction, &QAction::triggered, this, &ProcessorTab::expandView);
    m_toolbar->addAction(m_fitViewAction);

    const QIcon tableIcon = QIcon(":/icons/spreadsheet.svg");
    m_pipelineTableAction = new QAction(tableIcon, "Show pipelining table", this);
    connect(m_pipelineTableAction, &QAction::triggered, this, &ProcessorTab::showPipeliningTable);
    m_toolbar->addAction(m_pipelineTableAction);
}

void ProcessorTab::processorSelection() {
    ProcessorSelectionDialog diag(m_handler);
    if (diag.exec()) {
        // New processor model was selected
        m_vsrtlWidget->clearDesign();
        m_handler.selectProcessor(diag.selectedID);
        m_vsrtlWidget->setDesign(m_handler.getProcessor());
        updateInstructionModel();
        update();
    }
}

void ProcessorTab::updateInstructionModel() {
    auto* oldModel = m_instrModel;
    m_instrModel = new InstructionModel(m_handler, this);

    // Update the instruction view according to the newly created model
    m_ui->instructionView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ui->instructionView->setModel(m_instrModel);

    // Only the instruction column should stretch
    m_ui->instructionView->horizontalHeader()->setMinimumSectionSize(1);
    m_ui->instructionView->horizontalHeader()->setSectionResizeMode(InstructionModel::Breakpoint,
                                                                    QHeaderView::ResizeToContents);
    m_ui->instructionView->horizontalHeader()->setSectionResizeMode(InstructionModel::PC,
                                                                    QHeaderView::ResizeToContents);
    m_ui->instructionView->horizontalHeader()->setSectionResizeMode(InstructionModel::Stage,
                                                                    QHeaderView::ResizeToContents);
    m_ui->instructionView->horizontalHeader()->setSectionResizeMode(InstructionModel::Instruction,
                                                                    QHeaderView::Stretch);

    connect(this, &ProcessorTab::update, m_instrModel, &InstructionModel::processorWasClocked);

    // Make the instruction view follow the instruction which is currently present in the first stage of the processor
    connect(m_instrModel, &InstructionModel::firstStageInstrChanged, this,
            &ProcessorTab::setInstructionViewCenterAddr);

    if (oldModel) {
        delete oldModel;
    }
}

void ProcessorTab::restart() {
    // Invoked when changes to binary simulation file has been made
    emit update();
    enableSimulatorControls();
}

void ProcessorTab::initRegWidget() {
    // Setup register widget
    m_ui->registerContainer->setRegPtr(Pipeline::getPipeline()->getRegPtr());
    m_ui->registerContainer->init();
}

void ProcessorTab::updateMetrics() {
    m_ui->cycleCount->setText(QString::number(Pipeline::getPipeline()->getCycleCount()));
    m_ui->nInstrExecuted->setText(QString::number(Pipeline::getPipeline()->getInstructionsExecuted()));
}

ProcessorTab::~ProcessorTab() {
    delete m_ui;
}

void ProcessorTab::expandView() {
    tmp_pipelineWidget->expandToView();
}

void ProcessorTab::run() {
    m_autoClockAction->setChecked(false);
    auto pipeline = Pipeline::getPipeline();
    RunDialog dialog(this);
    if (pipeline->isReady()) {
        if (dialog.exec() && pipeline->isFinished()) {
            emit update();
            enableSimulatorControls();
        }
    }
}

void ProcessorTab::processorFinished() {
    // Disallow further clocking of the circuit
    m_clockAction->setEnabled(false);
    m_autoClockAction->setChecked(false);
    m_autoClockAction->setEnabled(false);
    m_runAction->setEnabled(false);
}

void ProcessorTab::enableSimulatorControls() {
    m_clockAction->setEnabled(true);
    m_autoClockAction->setEnabled(true);
    m_runAction->setEnabled(true);
    m_reverseAction->setEnabled(true);
    m_resetAction->setEnabled(true);
}

void ProcessorTab::reset() {
    m_autoClockAction->setChecked(false);
    m_vsrtlWidget->reset();
    Pipeline::getPipeline()->restart();
    emit update();

    enableSimulatorControls();
    emit appendToLog("\n");
}

void ProcessorTab::setInstructionViewCenterAddr(uint32_t address) {
    const auto row = address / 4;
    const auto instructionView = m_ui->instructionView;
    const auto rect = instructionView->rect();
    const int indexTop = instructionView->indexAt(rect.topLeft()).row();
    const int indexBot = instructionView->indexAt(rect.bottomLeft()).row();
    const int nItems = indexBot - indexTop;

    // move scrollbar if if is not visible
    if (row <= indexTop || row >= indexBot) {
        auto scrollbar = m_ui->instructionView->verticalScrollBar();
        scrollbar->setValue(row - nItems / 2);
    }
}

void ProcessorTab::rewind() {
    m_vsrtlWidget->rewind();
    enableSimulatorControls();
    emit update();
}

void ProcessorTab::clock() {
    m_vsrtlWidget->clock();
    m_handler.checkValidExecutionRange();

    auto pipeline = Pipeline::getPipeline();
    auto state = pipeline->step();

    const auto ecallVal = pipeline->checkEcall(true);
    if (ecallVal.first != Pipeline::ECALL::none) {
        handleEcall(ecallVal);
    }

    emit update();

    // Pipeline has finished executing
    /*
    if (pipeline->isFinished() || (state == 1 && ecallVal.first == Pipeline::ECALL::exit)) {
        enableSimulatorControls();
    }
    */
}

bool ProcessorTab::handleEcall(const std::pair<Pipeline::ECALL, int32_t>& ecall_val) {
    // Check if ecall has been invoked
    switch (ecall_val.first) {
        case Pipeline::ECALL::none:
            break;
        case Pipeline::ECALL::print_string: {
            // emit appendToLog(Parser::getParser()->getStringAt(static_cast<uint32_t>(ecall_val.second)));
            break;
        }
        case Pipeline::ECALL::print_int: {
            // emit appendToLog(QString::number(ecall_val.second));
            break;
        }
        case Pipeline::ECALL::print_char: {
            // emit appendToLog(QChar(ecall_val.second));
            break;
        }
        case Pipeline::ECALL::exit: {
            return true;  // The simulator will now take a few cycles to stop
        }
    }

    return true;  // continue
}

void ProcessorTab::showPipeliningTable() {
    // Setup pipeline table window
    PipelineTable window;
    PipelineTableModel model;
    window.setModel(&model);
    window.exec();
}
