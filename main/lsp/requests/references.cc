#include "main/lsp/requests/references.h"
#include "core/lsp/QueryResponse.h"
#include "main/lsp/LSPLoop.h"
#include "main/lsp/LSPOutput.h"
#include "main/lsp/LSPQuery.h"
#include "main/lsp/ShowOperation.h"
#include "main/lsp/json_types.h"
#include "packager/packager.h"

using namespace std;

namespace sorbet::realmain::lsp {

ReferencesTask::ReferencesTask(const LSPConfiguration &config, MessageId id, std::unique_ptr<ReferenceParams> params)
    : LSPRequestTask(config, move(id), LSPMethod::TextDocumentReferences), params(move(params)) {}

bool ReferencesTask::needsMultithreading(const LSPIndexer &indexer) const {
    return true;
}

vector<core::SymbolRef> ReferencesTask::getSymsToCheckWithinPackage(const core::GlobalState &gs,
                                                                    core::SymbolRef symInPackage,
                                                                    core::packages::MangledName packageName) {
    std::vector<core::NameRef> fullName;

    auto sym = symInPackage;
    while (sym.exists() && sym != core::Symbols::PackageSpecRegistry() && sym != core::Symbols::root()) {
        fullName.emplace_back(sym.name(gs));
        sym = sym.owner(gs);
    }
    reverse(fullName.begin(), fullName.end());

    vector<core::SymbolRef> result;
    vector<core::SymbolRef> namespacesToCheck = {
        core::Symbols::root(),
        core::Symbols::root().data(gs)->findMember(gs, core::packages::PackageDB::TEST_NAMESPACE),
    };

    for (auto &namespaceToCheck : namespacesToCheck) {
        if (!namespaceToCheck.exists()) {
            continue;
        }

        auto symFound = findSym(gs, fullName, namespaceToCheck);
        // Do nothing if the symbol is not found or is from the same package -- i.e. for class ... < PackageSpec
        // declarations
        if (symFound.exists() && gs.packageDB().getPackageNameForFile(symFound.loc(gs).file()) != packageName) {
            result.emplace_back(std::move(symFound));
        }
    }

    return result;
}

core::SymbolRef ReferencesTask::findSym(const core::GlobalState &gs, const vector<core::NameRef> &fullName,
                                        core::SymbolRef underNamespace) {
    core::SymbolRef symToCheck = underNamespace;
    for (auto &part : fullName) {
        symToCheck = symToCheck.asClassOrModuleRef().data(gs)->findMember(gs, part);
        if (!symToCheck.exists()) {
            return symToCheck;
        }
    }

    return symToCheck;
}

vector<std::unique_ptr<Location>>
ReferencesTask::getLocationsFromQueryResponse(LSPTypecheckerDelegate &typechecker, const core::GlobalState &gs,
                                              core::FileRef fref, bool fileIsTyped,
                                              std::unique_ptr<core::lsp::QueryResponse> resp) {
    // If file is untyped, only supports find reference requests from constants and class definitions.
    if (auto constResp = resp->isConstant()) {
        if (fref.data(gs).isPackage()) {
            // Special handling for package files.
            //
            // Case 1. get-refs on a package declaration
            //   class Foo < PackageSpec
            //         ^^^
            //   Returns all `import Foo` statements, globally
            //
            // Case 2. get-refs on an import statement
            //
            //  class Foo < PackageSpec
            //    ...
            //
            //    import Bar
            //          ^^^
            //  Returns all usages of `Bar` or `Test::Bar` *within* the Foo package only.
            //
            // Case 3. get-refs on an export statement
            //
            //  class Foo < PackageSpec
            //    ...
            //
            //    export Foo::A
            //                ^
            //  Returns all global usages of Foo::A

            auto packageName = gs.packageDB().getPackageNameForFile(fref);
            auto symsToCheck = getSymsToCheckWithinPackage(gs, constResp->symbolBeforeDealias, packageName);

            if (!symsToCheck.empty()) {
                std::vector<std::unique_ptr<Location>> locations;

                for (auto &symToCheck : symsToCheck) {
                    for (auto &location :
                         extractLocations(typechecker.state(),
                                          getReferencesToSymbolInPackage(typechecker, packageName, symToCheck))) {
                        locations.emplace_back(std::move(location));
                    }
                }

                return locations;
            } else {
                // Fall back to normal case when we are not querying for an external symbol, e.g. class Foo <
                // PackageSpec declarations, or export statements.
                return extractLocations(typechecker.state(),
                                        getReferencesToSymbol(typechecker, constResp->symbolBeforeDealias));
            }
        } else {
            // Normal handling for non-package files
            return extractLocations(typechecker.state(),
                                    getReferencesToSymbol(typechecker, constResp->symbolBeforeDealias));
        }
    } else if (auto fieldResp = resp->isField()) {
        // This could be a `prop` or `attr_*`, which have multiple associated symbols.
        return extractLocations(typechecker.state(),
                                getReferencesToAccessor(typechecker,
                                                        getAccessorInfo(typechecker.state(), fieldResp->symbol),
                                                        fieldResp->symbol));
    } else if (auto defResp = resp->isMethodDef()) {
        if (fileIsTyped) {
            // This could be a `prop` or `attr_*`, which have multiple associated symbols.
            return extractLocations(typechecker.state(),
                                    getReferencesToAccessor(typechecker,
                                                            getAccessorInfo(typechecker.state(), defResp->symbol),
                                                            defResp->symbol));
        } else {
            this->notifyAboutUntypedFile = true;
        }
    } else if (auto identResp = resp->isIdent()) {
        if (fileIsTyped) {
            auto loc = identResp->termLoc;
            if (loc.exists()) {
                auto run2 = typechecker.query(core::lsp::Query::createVarQuery(identResp->enclosingMethod,
                                                                               identResp->enclosingMethodLoc,
                                                                               identResp->variable),
                                              {loc.file()});
                return extractLocations(gs, run2.responses);
            }
        } else {
            this->notifyAboutUntypedFile = true;
        }
    } else if (auto sendResp = resp->isSend()) {
        if (fileIsTyped) {
            auto start = sendResp->dispatchResult.get();
            vector<std::unique_ptr<core::lsp::QueryResponse>> responses;
            while (start != nullptr) {
                if (start->main.method.exists() && !start->main.receiver.isUntyped()) {
                    // This could be a `prop` or `attr_*`, which has multiple associated symbols.
                    responses =
                        getReferencesToAccessor(typechecker, getAccessorInfo(typechecker.state(), start->main.method),
                                                start->main.method, move(responses));
                }
                start = start->secondary.get();
            }
            return extractLocations(typechecker.state(), responses);
        } else {
            this->notifyAboutUntypedFile = true;
        }
    }

    return {};
}

std::unique_ptr<ResponseMessage> ReferencesTask::runRequest(LSPTypecheckerDelegate &typechecker) {
    auto response = make_unique<ResponseMessage>("2.0", id, LSPMethod::TextDocumentReferences);
    ShowOperation op(config, ShowOperation::Kind::References);

    const core::GlobalState &gs = typechecker.state();
    auto result = LSPQuery::byLoc(config, typechecker, params->textDocument->uri, *params->position,
                                  LSPMethod::TextDocumentReferences, false);
    if (result.error) {
        // An error happened while setting up the query.
        response->error = move(result.error);
        return response;
    }

    // An explicit null indicates that we don't support this request (or that nothing was at the location).
    // Note: Need to correctly type variant here so it goes into right 'slot' of result variant.
    response->result = variant<JSONNullObject, vector<unique_ptr<Location>>>(JSONNullObject());
    auto &queryResponses = result.responses;
    core::FileRef fref = config.uri2FileRef(gs, params->textDocument->uri);
    bool fileIsTyped = false;
    if (fref.exists()) {
        fileIsTyped = fref.data(gs).strictLevel >= core::StrictLevel::True;
    }
    if (!queryResponses.empty()) {
        for (auto &resp : queryResponses) {
            if (auto defResp = resp->isMethodDef()) {
                cout << params->textDocument->uri << " " << params->position->showRaw() << " METHOD " << defResp->symbol.showFullName(gs) << endl;
            } else if (auto fieldResp = resp->isField()) {
                cout << params->textDocument->uri << " " << params->position->showRaw() << " METHOD " << fieldResp->symbol.showFullName(gs) << endl;
            } else if (resp->isLiteral()) {
                cout << params->textDocument->uri << " " << params->position->showRaw() << " LIT" << endl;
            } else if (auto identResp = resp->isIdent()) {
                cout << params->textDocument->uri << " " << params->position->showRaw() << " IDENT " << identResp->variable.showRaw(gs) << endl;
            }
        }

        vector<unique_ptr<Location>> resultLocations;
        auto responses = getQueryResponsesForFindAllReferences(queryResponses);

        for (auto &resp : move(responses)) {
            auto locations = getLocationsFromQueryResponse(typechecker, gs, fref, fileIsTyped, move(resp));
            absl::c_move(move(locations), back_inserter(resultLocations));
        }

        response->result = move(resultLocations);
    } else if (fref.exists() && !fileIsTyped) {
        // The first check ensures that the file actually exists (and therefore
        // we could have gotten responses) and the second check is what we are
        // actually interested in.
        this->notifyAboutUntypedFile = true;
    }

    if (this->notifyAboutUntypedFile) {
        ENFORCE(fref.exists());
        auto level = fref.data(gs).strictLevel;
        ENFORCE(level < core::StrictLevel::True);
        string asString = level == core::StrictLevel::Ignore ? "ignore" : "false";
        auto msg = fmt::format("File is `# typed: {}`, could not determine references", asString);
        auto params = make_unique<ShowMessageParams>(MessageType::Info, msg);
        this->config.output->write(make_unique<LSPMessage>(
            make_unique<NotificationMessage>("2.0", LSPMethod::WindowShowMessage, move(params))));
    }
    return response;
}

} // namespace sorbet::realmain::lsp
