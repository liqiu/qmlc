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

#include <QQmlEngine>

#include <private/qv4engine_p.h>
#include <private/qqmltypeloader_p.h>
#include <private/qqmltypecompiler_p.h>
#include <private/qqmlimport_p.h>
#include <private/qqmlvmemetaobject_p.h>

#include "qmctypeunit.h"
#include "qmcunit.h"
#include "qmcunitpropertycachecreator.h"
#include "qmcloader.h"
#include "qmcscriptunit.h"
#include "qmctypeunitcomponentandaliasresolver.h"

QmcTypeUnit::QmcTypeUnit(QmcUnit *qmcUnit, QQmlTypeLoader *typeLoader)
    : Blob(qmcUnit->url, QQmlDataBlob::QmlFile, typeLoader),
      unit(qmcUnit),
      compiledData(new QQmlCompiledData(qmcUnit->engine)),
      linked(false),
      vmeMetaObjects(compiledData->metaObjects),
      propertyCaches(compiledData->propertyCaches),
      doneLinking(false)
{
}


QmcTypeUnit::~QmcTypeUnit()
{
    foreach (const QQmlTypeData::ScriptReference &ref, scripts) {
        ref.script->release();
    }
    scripts.clear();

    foreach (QmcUnit *unit, dependencies) {
        unit->blob->release();
    }

    compiledData->release();
    delete unit;
}

QmcUnit *QmcTypeUnit::qmcUnit()
{
    return unit;
}

void QmcTypeUnit::initializeFromCachedUnit(const QQmlPrivate::CachedQmlUnit *)
{
}

void QmcTypeUnit::dataReceived(const Data &)
{
}

void QmcTypeUnit::done()
{
}

QString QmcTypeUnit::stringAt(int idx) const
{
    return unit->stringAt(idx);
}

bool QmcTypeUnit::link()
{
    if (linked)
        return true;

    linked = true;

    // create QV4::CompiledData::CompilationUnit and QQmlCompiledData
    compiledData->compilationUnit = unit->compilationUnit;
    compiledData->compilationUnit->ref();
    compiledData->qmlUnit = unit->qmlUnit;
    compiledData->name = unit->name;

    setStatus(Complete);

    // create imports
    if (!addImports())
        return false;

    // resolve dependencies (TBD: recursively)
    if (!initDependencies())
        return false;

    if (!unit->makeExecutable())
        return false;

    if (!initQml())
        return false;

    return true;
}

bool QmcTypeUnit::addImports()
{
    m_importCache.setBaseUrl(unit->url, unit->urlString);

    compiledData->customParserData = qmcUnit()->customParsers;
    compiledData->customParserBindings = qmcUnit()->customParserBindings;
    compiledData->deferredBindingsPerObject = qmcUnit()->deferredBindings;

    for (uint i = 0; i < compiledData->qmlUnit->nImports; i++) {
        const QV4::CompiledData::Import *p = compiledData->qmlUnit->importAt(i);
        if (p->type == QV4::CompiledData::Import::ImportScript) {
            // load it if it does not exist yet
            QmcScriptUnit* scriptUnit = unit->loader->getScript(stringAt(p->uriIndex), finalUrl());
            if (!unit) {
                QQmlError error;
                error.setColumn(p->location.column);
                error.setLine(p->location.line);
                error.setUrl(finalUrl());
                error.setDescription("Could not find imported script");

                return false;
            }
            QQmlTypeData::ScriptReference ref;
            ref.location = p->location;
            ref.qualifier = stringAt(p->qualifierIndex);
            ref.script = scriptUnit;
            scripts.append(ref);
        }
        if (p->type == QV4::CompiledData::Import::ImportLibrary) {
            if (!addImport(p, &unit->errors))
                return false;
        }
    }

    // resolve types
    // type data creation
    // qqmlirbuilder.cpp:285 create QV4::CompiledData::TypeReference = location
    // qqmltypeloader.cpp:2400 QV4::CompiledData::TypeReference -> QQmlTypeData::TypeReference
    // qqmltypecompiler.cpp:86 QQmlTypeData::TypeReference -> QQmlCompiledData::TypeReference

    foreach (const QmcUnitTypeReference& typeRef, unit->typeReferences) {
        int majorVersion = -1;
        int minorVersion = -1;
        QQmlImportNamespace *typeNamespace = 0;
        if ((int)typeRef.index >= unit->strings.size())
            return false;
        const QString name = stringAt(typeRef.index);
        if (typeRef.syntheticComponent)
            continue;
        QQmlCompiledData::TypeReference *ref = new QQmlCompiledData::TypeReference;
        if (!m_importCache.resolveType(name, &ref->type, &majorVersion, &minorVersion, &typeNamespace, &unit->errors)) {
            // try to load it
            QmcUnit *typeUnit = qmcUnit()->loader->getType(name, finalUrl());
            if (typeUnit) {
                ref->component = ((QmcTypeUnit *)typeUnit->blob)->refCompiledData(); // addref
                unit->errors.clear();
                dependencies.append(typeUnit);
            }
            if (!typeUnit) {
                delete ref;
                return false;
            }
        }

        // TBD component creation, see qqmltypecompiler.cpp:87
        if (ref->type) {
            if (ref->type->containsRevisionedAttributes()) {
                // qqmltypecompiler.cpp:102
                QQmlError cacheError;
                ref->typePropertyCache = QQmlEnginePrivate::get(unit->engine)->cache(ref->type, minorVersion, cacheError);

                if (!ref->typePropertyCache) {
                    delete ref;
                    unit->errors.append(cacheError);
                    return false;
                }
                ref->typePropertyCache->addref();
            }
        }
        // TBD: qqmltypecompiler.cpp:86

        ref->majorVersion = majorVersion;
        ref->minorVersion = minorVersion;

        // dynamic type check moved to init phase

        compiledData->resolvedTypes.insert(typeRef.index, ref);
    }

    // from QQmlTypeCompiler::compile
    compiledData->importCache = new QQmlTypeNameCache;

    foreach (const QString &ns, unit->namespaces)
        compiledData->importCache->add(ns);
#if 0
    // Add any Composite Singletons that were used to the import cache
    foreach (const QQmlTypeData::TypeReference &singleton, compositeSingletons)
        compiledData->importCache->add(singleton.type->qmlTypeName(), singleton.type->sourceUrl(), singleton.prefix);
#endif

    // TBD: is import cache required at all ? Cannot be null though.
    m_importCache.populateCache(compiledData->importCache);

    // qqmltypecompiler.cpp:1413
    foreach (const QmcUnitTypeReference& typeRef, unit->typeReferences) {
        static QQmlType *componentType = QQmlMetaType::qmlType(&QQmlComponent::staticMetaObject);
        if (!typeRef.syntheticComponent)
            continue;
        QQmlCompiledData::TypeReference *ref = new QQmlCompiledData::TypeReference;
        ref->type = componentType;
        ref->majorVersion = componentType->majorVersion();
        ref->minorVersion = componentType->minorVersion();

        compiledData->resolvedTypes.insert(typeRef.index, ref);
    }

    return true;
}

QQmlCompiledData *QmcTypeUnit::refCompiledData()
{
    Q_ASSERT(compiledData);
    compiledData->addref();
    return compiledData;
}

bool QmcTypeUnit::initDependencies()
{
    foreach (const QQmlTypeData::ScriptReference &scriptRef, scripts) {
        QmcScriptUnit *script = (QmcScriptUnit *)scriptRef.script;
        if (!script->initialize())
            return false;
    }

    foreach (QmcUnit *unit, dependencies) {
        if (unit->type == QMC_QML) {
            QmcTypeUnit * blob = (QmcTypeUnit *)unit->blob;
            if (!blob->link())
                return false;
        }
    }

    // TBD: initialize dependencies of script & types (recursion)
    return true;
}

bool QmcTypeUnit::initQml()
{
    // type references init
    foreach (QQmlCompiledData::TypeReference *typeRef, compiledData->resolvedTypes) {
        typeRef->doDynamicTypeCheck();
    }

    compiledData->initialize(unit->engine);

    // create property caches
    // qqmltypecompiler.cpp:143-150
    QmcUnitPropertyCacheCreator cacheCreator(this);
    if (!cacheCreator.buildMetaObjects())
        return false;

    // scripts
    // qqmltypeloader.cpp:1315:
    // create QQmlScriptBlob + QQmlScriptData
    // add to QQmlTypeLoader::Blob->scripts
    // qqmltypecompiler.cpp:180:
    // add to import cache
    // QQmlTypeData::ScriptReference->scriptData -> compiledData->scripts

    foreach (const QQmlTypeData::ScriptReference &scriptRef, scripts) {
        // create QQmlScriptData and link it to QmcScriptUnit
        QQmlScriptData *scriptData = scriptRef.script->scriptData();
        scriptData->addref();
        compiledData->scripts.append(scriptData);
    }

    // add object mappings + aliases
    // alias creation call chain
    // QQmlTypeCompiler::compile
    // ->QQmlComponentAndAliasResolver::resolve->resolveAliases
    // -->QQmlPropertyCache::appendProperty
    // TBD: add aliases to property cache
    QmcTypeUnitComponentAndAliasResolver resolver(this);
    if (!resolver.resolve())
        return false;

    // TBD: alias creation makes component composite type
    if (compiledData->isCompositeType()) {
        // TBD: does this work ?
        QQmlEnginePrivate::get(unit->engine)->registerInternalCompositeType(compiledData);
        //engine->registerInternalCompositeType(compiledData);
    } else {
        const QV4::CompiledData::Object *obj = compiledData->qmlUnit->objectAt(compiledData->qmlUnit->indexOfRootObject);
        if (!obj)
            return false;
        QQmlCompiledData::TypeReference *typeRef = compiledData->resolvedTypes.value(obj->inheritedTypeNameIndex);
        if (!typeRef)
            return false;

        if (typeRef->component) {
            compiledData->metaTypeId = typeRef->component->metaTypeId;
            compiledData->listMetaTypeId = typeRef->component->listMetaTypeId;
        } else {
            compiledData->metaTypeId = typeRef->type->typeId();
            compiledData->listMetaTypeId = typeRef->type->qListTypeId();
        }
    }

    // extra initiliazation of QQmlCompiledData qqmltypecompiler.cpp:263
#if 0
    // TBD: function below seems to quite a lot
    // Sanity check property bindings
    QQmlPropertyValidator validator(this);
    if (!validator.validate())
        return false;
#endif
    // add custom parsers, custom bindings and deferred bindings
    // TBD:

    // Collect some data for instantiation later.
    int bindingCount = 0;
    int parserStatusCount = 0;
    int objectCount = 0;
    for (quint32 i = 0; i < compiledData->qmlUnit->nObjects; ++i) {
        const QV4::CompiledData::Object *obj = compiledData->qmlUnit->objectAt(i);
        bindingCount += obj->nBindings;
        if (QQmlCompiledData::TypeReference *typeRef = compiledData->resolvedTypes.value(obj->inheritedTypeNameIndex)) {
            if (QQmlType *qmlType = typeRef->type) {
                if (qmlType->parserStatusCast() != -1)
                    ++parserStatusCount;
            }
            if (typeRef->component) {
                bindingCount += typeRef->component->totalBindingsCount;
                parserStatusCount += typeRef->component->totalParserStatusCount;
                objectCount += typeRef->component->totalObjectCount;
            } else
                ++objectCount;
        }
    }

    compiledData->totalBindingsCount = bindingCount;
    compiledData->totalParserStatusCount = parserStatusCount;
    compiledData->totalObjectCount = objectCount;
    if (compiledData->propertyCaches.count() != static_cast<int>(compiledData->qmlUnit->nObjects))
        return false;

    return true;
}

QQmlComponent* QmcTypeUnit::createComponent()
{
    QQmlComponent* component = new QQmlComponent(unit->engine);
    QQmlComponentPrivate* cPriv = QQmlComponentPrivate::get(component);
    cPriv->cc = compiledData;
    cPriv->cc->addref();
    return component;
}