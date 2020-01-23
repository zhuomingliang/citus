/*-------------------------------------------------------------------------
 *
 * deparser.h
 *	  Used when deparsing any ddl parsetree into its sql from.
 *
 * Copyright (c) Citus Data, Inc.
 *
 * $Id$
 *
 *-------------------------------------------------------------------------
 */

#ifndef CITUS_DEPARSER_H
#define CITUS_DEPARSER_H

#include "postgres.h"

#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "catalog/objectaddress.h"

/* forward declarations for format_collate.c */
/* Control flags for FormatCollateExtended, compatible with format_type_extended */
#define FORMAT_COLLATE_ALLOW_INVALID 0x02       /* allow invalid types */
#define FORMAT_COLLATE_FORCE_QUALIFY 0x04       /* force qualification of collate */
extern char * FormatCollateBE(Oid collate_oid);
extern char * FormatCollateBEQualified(Oid collate_oid);
extern char * FormatCollateExtended(Oid collid, bits16 flags);

extern void AssertObjectTypeIsFunctional(ObjectType type);

extern void QualifyTreeNode(Node *stmt);
extern char * DeparseTreeNode(const Node *stmt);

/* forward declarations for deparse_attribute_stmts.c */
extern char * DeparseRenameAttributeStmt(const Node *);

/* forward declarations for deparse_collation_stmts.c */
extern char * DeparseDropCollationStmt(const Node *stmt);
extern char * DeparseRenameCollationStmt(const Node *stmt);
extern char * DeparseAlterCollationSchemaStmt(const Node *stmt);
extern char * DeparseAlterCollationOwnerStmt(const Node *stmt);

extern void QualifyDropCollationStmt(Node *stmt);
extern void QualifyRenameCollationStmt(Node *stmt);
extern void QualifyAlterCollationSchemaStmt(Node *stmt);
extern void QualifyAlterCollationOwnerStmt(Node *stmt);

/* forward declarations for deparse_table_stmts.c */
extern char * DeparseAlterTableSchemaStmt(const Node *stmt);

extern void QualifyAlterTableSchemaStmt(Node *stmt);

/* forward declarations for deparse_schema_stmts.c */
extern char * DeparseGrantOnSchemaStmt(const Node *stmt);

/* forward declarations for deparse_type_stmts.c */
extern char * DeparseCompositeTypeStmt(const Node *stmt);
extern char * DeparseCreateEnumStmt(const Node *stmt);
extern char * DeparseDropTypeStmt(const Node *stmt);
extern char * DeparseAlterEnumStmt(const Node *stmt);
extern char * DeparseAlterTypeStmt(const Node *stmt);
extern char * DeparseRenameTypeStmt(const Node *stmt);
extern char * DeparseRenameTypeAttributeStmt(const Node *stmt);
extern char * DeparseAlterTypeSchemaStmt(const Node *stmt);
extern char * DeparseAlterTypeOwnerStmt(const Node *stmt);

extern void QualifyRenameAttributeStmt(Node *stmt);
extern void QualifyRenameTypeStmt(Node *stmt);
extern void QualifyRenameTypeAttributeStmt(Node *stmt);
extern void QualifyAlterEnumStmt(Node *stmt);
extern void QualifyAlterTypeStmt(Node *stmt);
extern void QualifyCompositeTypeStmt(Node *stmt);
extern void QualifyCreateEnumStmt(Node *stmt);
extern void QualifyAlterTypeSchemaStmt(Node *stmt);
extern void QualifyAlterTypeOwnerStmt(Node *stmt);

extern ObjectAddress GetObjectAddressFromParseTree(const Node *parseTree, bool
												   missing_ok);
extern ObjectAddress RenameAttributeStmtObjectAddress(const Node *stmt, bool missing_ok);

/* forward declarations for deparse_function_stmts.c */
extern char * DeparseDropFunctionStmt(const Node *stmt);
extern char * DeparseAlterFunctionStmt(const Node *stmt);

extern char * DeparseRenameFunctionStmt(const Node *stmt);
extern char * DeparseAlterFunctionSchemaStmt(const Node *stmt);
extern char * DeparseAlterFunctionOwnerStmt(const Node *stmt);
extern char * DeparseAlterFunctionDependsStmt(const Node *stmt);

extern void QualifyAlterFunctionStmt(Node *stmt);
extern void QualifyRenameFunctionStmt(Node *stmt);
extern void QualifyAlterFunctionSchemaStmt(Node *stmt);
extern void QualifyAlterFunctionOwnerStmt(Node *stmt);
extern void QualifyAlterFunctionDependsStmt(Node *stmt);

/* forward declarations for deparse_role_stmts.c */
extern char * DeparseAlterRoleStmt(const Node *stmt);

/* forward declarations for deparse_extension_stmts.c */
extern Value * GetExtensionOption(List *extensionOptions,
								  const char *defname);
extern char * DeparseCreateExtensionStmt(const Node *stmt);
extern char * DeparseDropExtensionStmt(const Node *stmt);
extern char * DeparseAlterExtensionSchemaStmt(const Node *stmt);
extern char * DeparseAlterExtensionStmt(const Node *stmt);

#endif /* CITUS_DEPARSER_H */
