/*!
 * Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
 * Contact: http://www.qt-project.org/legal
 * Copyright (C) 2014 Nomovok Ltd. All rights reserved.
 * Contact: info@nomovok.com
 *
 * This file may be used under the terms of the GNU Lesser
 * General Public License version 2.1 as published by the Free Software
 * Foundation and appearing in the file LICENSE.LGPL included in the
 * packaging of this file.  Please review the following information to
 * ensure the GNU Lesser General Public License version 2.1 requirements
 * will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 *
 * In addition, as a special exception, Digia and other copyright holders
 * give you certain additional rights.  These rights are described in
 * the Digia Qt LGPL Exception version 1.1, included in the file
 * LGPL_EXCEPTION.txt in this package.
 */

#include "qmcinstructionselection.h"
#include "qmclinktable.h"
#include <private/qv4ssa_p.h>

#include "AbstractMacroAssembler.h"

using namespace QV4;
using namespace QV4::IR;
using namespace QV4::JIT;

QmcInstructionSelection::QmcInstructionSelection(QQmlEnginePrivate *qmlEngine, QV4::ExecutableAllocator *execAllocator,
                                                 QV4::IR::Module *module, QV4::Compiler::JSUnitGenerator *jsGenerator)
    : QV4::JIT::InstructionSelection(qmlEngine, execAllocator, module, jsGenerator)
{
}

void QmcInstructionSelection::run(int functionIndex)
{
    IR::Function *function = irModule->functions[functionIndex];
    QVector<Lookup> lookups;
    qSwap(_function, function);

    //qDebug() << "Compile" << *_function->name;

    IR::Optimizer opt(_function);
    opt.run(qmlEngine);

#ifdef REGALLOC_IS_SUPPORTED
    static const bool withRegisterAllocator = qgetenv("QV4_NO_REGALLOC").isEmpty();
    if (opt.isInSSA() && withRegisterAllocator) {
        RegisterAllocator(getIntRegisters(), getFpRegisters()).run(_function, opt);
    } else
#endif // REGALLOC_IS_SUPPORTED
    {
        if (opt.isInSSA())
            // No register allocator available for this platform, or env. var was set, so:
            opt.convertOutOfSSA();
        ConvertTemps().toStackSlots(_function);
    }

    if (!qgetenv("QV4_SHOW_IR_ONLY_USER").isNull() && qgetenv("QV4_SHOW_IR_ONLY_FINAL").isNull()) {
        if (*_function->name != QString("%entry") && *_function->name != QString("freeze_recur") && _function->name != QString("context scope"))
            IR::Optimizer::showMeTheCode(_function);
    } else
        IR::Optimizer::showMeTheCode(_function);

    QSet<IR::Jump *> removableJumps = opt.calculateOptionalJumps();
    qSwap(_removableJumps, removableJumps);

    Assembler* oldAssembler = _as;
    _as = new Assembler(this, _function, executableAllocator, 6); // 6 == max argc for calls to built-ins with an argument array

    _as->enterStandardStackFrame();

#ifdef ARGUMENTS_IN_REGISTERS
    _as->move(_as->registerForArgument(0), Assembler::ContextRegister);
#else
    _as->loadPtr(addressForArgument(0), Assembler::ContextRegister);
#endif

    const int locals = _as->stackLayout().calculateJSStackFrameSize();
    _as->loadPtr(Address(Assembler::ContextRegister, qOffsetOf(ExecutionContext, engine)), Assembler::ScratchRegister);
    _as->loadPtr(Address(Assembler::ScratchRegister, qOffsetOf(ExecutionEngine, jsStackTop)), Assembler::LocalsRegister);
    _as->addPtr(Assembler::TrustedImm32(sizeof(QV4::Value)*locals), Assembler::LocalsRegister);
    _as->storePtr(Assembler::LocalsRegister, Address(Assembler::ScratchRegister, qOffsetOf(ExecutionEngine, jsStackTop)));

    int lastLine = 0;
#if QT_VERSION > QT_VERSION_CHECK(5,3,0)
    for (int i = 0, ei = _function->basicBlockCount(); i != ei; ++i) {
        IR::BasicBlock *nextBlock = (i < ei - 1) ? _function->basicBlock(i + 1) : 0;
        _block = _function->basicBlock(i);
#else
    for (int i = 0, ei = _function->basicBlocks.size(); i != ei; ++i) {
        IR::BasicBlock *nextBlock = (i < ei - 1) ? _function->basicBlocks[i + 1] : 0;
        _block = _function->basicBlocks[i];
#endif
        _as->registerBlock(_block, nextBlock);

#if QT_VERSION > QT_VERSION_CHECK(5,3,0)
        foreach (IR::Stmt *s, _block->statements()) {
#else
        foreach (IR::Stmt *s, _block->statements) {
#endif
            if (s->location.isValid()) {
                if (int(s->location.startLine) != lastLine) {
                    Assembler::Address lineAddr(Assembler::ContextRegister, qOffsetOf(QV4::ExecutionContext, lineNumber));
                    _as->store32(Assembler::TrustedImm32(s->location.startLine), lineAddr);
                    lastLine = s->location.startLine;
                }
            }
            s->accept(this);
        }
    }

    if (!_as->exceptionReturnLabel.isSet())
        visitRet(0);

    int dummySize;
    // check data
    QVector<QmcUnitCodeRefLinkCall> calls;
    Q_ASSERT(linkedCalls.size() == functionIndex);
    QList<Assembler::CallToLink>& callsToLink = _as->callsToLink();
    for (int i = 0; i < callsToLink.size(); i++) {
        Assembler::CallToLink& ctl = callsToLink[i];
        QString fname (ctl.functionName);
        //qDebug() << "Call to link " << fname << "ext value" << ctl.externalFunction.value();
        // find entry
        int index = -1;
        for (uint i = 0; i < sizeof(QMC_LINK_TABLE) / sizeof (QmcLinkEntry); i++) {
            if (QMC_LINK_TABLE[i].addr == ctl.externalFunction.value()) {
                index = i;
                break;
            }
        }

        if (ctl.call.isFlagSet(Assembler::Call::Near)) {
            qDebug() << "Near linker flag is not supported" << ctl.functionName;
            Q_ASSERT(0);
        }

        if (index < 0) {
            // try name based look up
            for (uint i = 0; i < sizeof (QMC_LINK_TABLE) / sizeof (QmcLinkEntry); i++) {
                const QmcLinkEntry& entry = QMC_LINK_TABLE[i];
                if (!strncmp(ctl.functionName, entry.name, strlen(entry.name))) {
                    index = i;
                    break;
                }
            }
            //Q_ASSERT(index >= 0);
        }
        Q_ASSERT(index >= 0);
        QmcUnitCodeRefLinkCall link;
        link.index = index;
        link.offset = ctl.call.m_label.m_offset;
        calls.append(link);
    }

    linkedCalls.append(calls);

    JSC::MacroAssemblerCodeRef codeRef =_as->link(&dummySize);
    compilationUnit->codeRefs[functionIndex] = codeRef;

    qSwap(_function, function);
    delete _as;
    _as = oldAssembler;
    qSwap(_removableJumps, removableJumps);

}
