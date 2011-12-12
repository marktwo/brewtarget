/*
 * database.cpp is part of Brewtarget, and is Copyright Philip G. Lee
 * (rocketman768@gmail.com), 2009-2011.
 *
 * Brewtarget is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * Brewtarget is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "database.h"

#include <QList>
#include <QDomDocument>
#include <QIODevice>
#include <QDomNodeList>
#include <QDomNode>
#include <QTextStream>
#include <QTextCodec>
#include <QObject>
#include <QString>
#include <QFileInfo>
#include <QFile>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlIndex>

#include "Algorithms.h"
#include "brewnote.h"
#include "equipment.h"
#include "fermentable.h"
#include "hop.h"
#include "instruction.h"
#include "mash.h"
#include "mashstep.h"
#include "misc.h"
#include "recipe.h"
#include "style.h"
#include "water.h"
#include "yeast.h"

#include "config.h"
#include "brewtarget.h"
#include "SetterCommand.h"

// Static members.
QFile Database::dbFile;
QString Database::dbFileName;
QFile Database::dataDbFile;
QString Database::dataDbFileName;
QHash<Database::DBTable,QString> Database::tableNames = Database::tableNamesHash();
QHash<QString,Database::DBTable> Database::classNameToTable = Database::classNameToTableHash();

Database::Database()
{
   commandStack.setUndoLimit(100);
   load();
}

void Database::load()
{
   bool dbIsOpen;
   bool createFromScratch = false;
   
   // Set file names.
   dbFileName = (Brewtarget::getUserDataDir() + "database.sqlite");
   dbFile.setFileName(dbFileName);
   dataDbFileName = (Brewtarget::getDataDir() + "database.sqlite");
   dataDbFile.setFileName(dataDbFileName);
   
   // If there's no dbFile, try to copy from dataDbFile.
   if( !dbFile.exists() )
   {
      // If there's no dataDbFile, create dbFile from scratch.
      if( !dataDbFile.exists() )
      {
         createFromScratch = true;
         // Right now, just create the file.
         dbFile.open( QIODevice::ReadWrite );
         dbFile.close();
      }
      else
      {
         dataDbFile.copy(dbFileName);
      }
   }
   
   // Open SQLite db.
   // http://www.developer.nokia.com/Community/Wiki/CS001504_-_Creating_an_SQLite_database_in_Qt
   sqldb.close();
   sqldb = QSqlDatabase::addDatabase("QSQLITE");
   sqldb.setDatabaseName(dbFileName);
   dbIsOpen = sqldb.open();
   if( ! dbIsOpen )
   {
      Brewtarget::logE(QString("Could not open %1 for reading.").arg(dbFileName));
      // TODO: if we can't open the database, what should we do?
      return;
   }
   
   // If we need to create from scratch, execute all the SQL commands necessary to do so.
   if( createFromScratch )
   {
      QFile createDbFile;
      createDbFile.setFileName(":/create_db.sql");
      createDbFile.open( QIODevice::ReadOnly );
      QString commands(createDbFile.readAll());
      createDbFile.close();
      
      QSqlQuery( commands, sqldb );
   }
   
   // See if there are new ingredients that we need to merge from the data-space db.
   if( dataDbFile.fileName() != dbFile.fileName()
      && ! Brewtarget::userDatabaseDidNotExist // Don't do this if we JUST copied the dataspace database.
      && QFileInfo(dataDbFile).lastModified() > Brewtarget::lastDbMergeRequest )
   {
      
      if( QMessageBox::question(0,
         QObject::tr("Merge Database"),
                                QObject::tr("There may be new ingredients and recipes available. Would you like to add these to your database?"),
                                QMessageBox::Yes | QMessageBox::No,
                                QMessageBox::Yes)
         == QMessageBox::Yes
         )
      {
         // TODO: implement merging here.
      }
      
      // Update this field.
      Brewtarget::lastDbMergeRequest = QDateTime::currentDateTime();
   }
   
   // Set up the tables.
   tableModel = new QSqlRelationalTableModel( 0, sqldb );
   tables.clear();
   
   equipments_tm = new QSqlRelationalTableModel( 0, sqldb );
   equipments_tm->setTable(tableNames[EQUIPTABLE]);
   tables[EQUIPTABLE] = equipments_tm;
   
   fermentables_tm = new QSqlRelationalTableModel( 0, sqldb );
   fermentables_tm->setTable(tableNames[FERMTABLE]);
   tables[FERMTABLE] = fermentables_tm;
   
   hops_tm = new QSqlRelationalTableModel( 0, sqldb );
   hops_tm->setTable(tableNames[HOPTABLE]);
   tables[HOPTABLE] = hops_tm;
   
   instructions_tm = new QSqlRelationalTableModel( 0, sqldb );
   instructions_tm->setTable(tableNames[INSTRUCTIONTABLE]);
   tables[INSTRUCTIONTABLE] = instructions_tm;
   
   mashs_tm = new QSqlRelationalTableModel( 0, sqldb );
   mashs_tm->setTable(tableNames[MASHTABLE]);
   tables[MASHTABLE] = mashs_tm;
   
   mashSteps_tm = new QSqlRelationalTableModel( 0, sqldb );
   mashSteps_tm->setTable(tableNames[MASHSTEPTABLE]);
   tables[MASHSTEPTABLE] = mashSteps_tm;
   
   miscs_tm = new QSqlRelationalTableModel( 0, sqldb );
   miscs_tm->setTable(tableNames[MISCTABLE]);
   tables[MISCTABLE] = miscs_tm;
   
   recipes_tm = new QSqlRelationalTableModel( 0, sqldb );
   recipes_tm->setTable(tableNames[RECTABLE]);
   tables[RECTABLE] = recipes_tm;
   
   styles_tm = new QSqlRelationalTableModel( 0, sqldb );
   styles_tm->setTable(tableNames[STYLETABLE]);
   tables[STYLETABLE] = styles_tm;
   
   waters_tm = new QSqlRelationalTableModel( 0, sqldb );
   waters_tm->setTable(tableNames[WATERTABLE]);
   tables[WATERTABLE] = waters_tm;
   
   yeasts_tm = new QSqlRelationalTableModel( 0, sqldb );
   yeasts_tm->setTable(tableNames[YEASTTABLE]);
   tables[YEASTTABLE] = yeasts_tm;
   
   // TODO: set relations?
   
   // Create and store all pointers.
   populateElements( allBrewNotes, brewnotes_tm, BREWNOTETABLE );
   populateElements( allEquipments, equipments_tm, EQUIPTABLE );
   populateElements( allFermentables, fermentables_tm, FERMTABLE );
   populateElements( allHops, hops_tm, HOPTABLE );
   populateElements( allInstructions, instructions_tm, INSTRUCTIONTABLE );
   populateElements( allMashs, mashs_tm, MASHTABLE );
   populateElements( allMashSteps, mashSteps_tm, MASHSTEPTABLE );
   populateElements( allMiscs, miscs_tm, MISCTABLE );
   populateElements( allRecipes, recipes_tm, RECTABLE );
   populateElements( allStyles, styles_tm, STYLETABLE );
   populateElements( allWaters, waters_tm, WATERTABLE );
   populateElements( allYeasts, yeasts_tm, YEASTTABLE );
}

Database& Database::instance()
{
   static Database dbSingleton;
      return dbSingleton;
}

bool Database::backupToDir(QString dir)
{
   // Make sure the singleton exists.
   instance();
   
   bool success = true;
   QString prefix = dir + "/";
   QString newDbFileName = prefix + "database.sqlite";
   
   // Remove the files if they already exist so that
   // the copy() operation will succeed.
   QFile::remove(newDbFileName);
   
   success = dbFile.copy( newDbFileName );
   
   return success;
}

bool Database::restoreFromDir(QString dirStr)
{
   bool success = true;
   
   // Ensure singleton exists.
   instance();
   
   QString prefix = dirStr + "/";
   QString newDbFileName = prefix + "database.sqlite";
   QFile newDbFile(newDbFileName);
   
   // Fail if we can't find file.
   if( !newDbFile.exists() )
      return false;
   
   // TODO: there are probably concurrency issues here. What if a query happens
   // between these two lines?
   success &= dbFile.remove();   
   success &= newDbFile.copy(dbFile.fileName());
   
   // Reload everything.
   instance().load();
   
   return success;
}

QVariant Database::get( DBTable table, int key, const char* col_name )
{
   QSqlQuery q( QString("SELECT %1 FROM %2 WHERE %3 = %4")
                .arg(col_name).arg(tableNames[table]).arg(keyName(table)).arg(key),
                sqldb );
   
   if( q.next() )
      return q.value(0);
   else
      return QVariant();
}

// removeFromRecipe ===========================================================
void Database::removeIngredientFromRecipe( Recipe* rec, BeerXMLElement* ing, QString propName, QString relTableName, QString ingKeyName )
{
   // TODO: encapsulate this in a QUndoCommand.
   
   tableModel->setTable(relTableName);
   QString filter = tableModel->filter();
   
   // Find the row in the relational db that connects the ingredient to the recipe.
   tableModel->setFilter( QString("%1=%2 AND recipe_id=%3").arg(ingKeyName).arg(ing->_key).arg(rec->_key) );
   tableModel->select();
   if( tableModel->rowCount() > 0 )
      tableModel->removeRows(0,1);
   
   // Restore the old filter.
   tableModel->setFilter(filter);
   tableModel->select();
   
   emit rec->changed( rec->metaProperty(propName), QVariant() );
}

void Database::removeFromRecipe( Recipe* rec, BrewNote* b )
{
   // Just mark the brew note as deleted.
   sqlUpdate( tableNames[BREWNOTETABLE],
              "deleted=TRUE",
              QString("%1=%2").arg(keyName(BREWNOTETABLE)).arg(b->_key) );
}

void Database::removeFromRecipe( Recipe* rec, Hop* hop )
{
   removeIngredientFromRecipe( rec, hop, "hops", "hop_in_recipe", "hop_id" );
}

void Database::removeFromRecipe( Recipe* rec, Fermentable* ferm )
{
   removeIngredientFromRecipe( rec, ferm, "fermentables", "fermentable_in_recipe", "fermentable_id" );
}

void Database::removeFromRecipe( Recipe* rec, Misc* m )
{
   removeIngredientFromRecipe( rec, m, "miscs", "misc_in_recipe", "misc_id" );
}

void Database::removeFromRecipe( Recipe* rec, Yeast* y )
{
   removeIngredientFromRecipe( rec, y, "yeasts", "yeast_in_recipe", "yeast_id" );
}

void Database::removeFromRecipe( Recipe* rec, Water* w )
{
   removeIngredientFromRecipe( rec, w, "waters", "water_in_recipe", "water_id" );
}

void Database::removeFromRecipe( Recipe* rec, Instruction* ins )
{
   // TODO: encapsulate in QUndoCommand.
   // NOTE: is this the right thing to do?
   sqlUpdate( tableNames[INSTRUCTIONTABLE],
              "deleted=TRUE",
              QString("%1=%2").arg(keyName(INSTRUCTIONTABLE)).arg(ins->_key) );
}
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void Database::removeFrom( Mash* mash, MashStep* step )
{
   // Just mark the step as deleted.
   sqlUpdate( tableNames[MASHSTEPTABLE],
              "deleted=TRUE",
              QString("%1=%2").arg(keyName(MASHSTEPTABLE)).arg(step->_key) );
}

Recipe* Database::getParentRecipe( BrewNote const* note )
{
   int key;
   QSqlQuery q( QString("SELECT recipe_id FROM brewnote WHERE id = %1").arg(note->_key),
                sqldb );
   q.next();
   key = q.record().value("recipe_id").toInt();
   
   return allRecipes[key];
}

Recipe* Database::recipe(int key)
{
   if( allRecipes.contains(key) )
      return allRecipes[key];
   else
      return 0;
}

Equipment* Database::equipment(int key)
{
   if( allEquipments.contains(key) )
      return allEquipments[key];
   else
      return 0;
}

Mash* Database::mash(int key)
{
   if( allMashs.contains(key) )
      return allMashs[key];
   else
      return 0;
}

Style* Database::style(int key)
{
   if( allStyles.contains(key) )
      return allStyles[key];
   else
      return 0;
}

void Database::swapMashStepOrder(MashStep* m1, MashStep* m2)
{
   // TODO: encapsulate in QUndoCommand.
   QSqlQuery q( QString("UPDATE mashstep SET step_number = CASE msid WHEN %1 then %2 when %3 then %4 END WHERE msid IN (%5,%6)")
                .arg(m1->_key).arg(m2->_key).arg(m2->_key).arg(m1->_key).arg(m1->_key).arg(m2->_key),
                sqldb );
                
   emit m1->changed( m1->metaProperty("stepNumber") );
   emit m2->changed( m2->metaProperty("stepNumber") );
}

void Database::swapInstructionOrder(Instruction* in1, Instruction* in2)
{
   // TODO: encapsulate in QUndoCommand.
   QSqlQuery q( QString("UPDATE instruction SET instruction_number = CASE iid WHEN %1 then %2 when %3 then %4 END WHERE iid IN (%5,%6)")
                .arg(in1->_key).arg(in2->_key).arg(in2->_key).arg(in1->_key).arg(in1->_key).arg(in2->_key),
                sqldb );
                
   emit in1->changed( in1->metaProperty("instructionNumber") );
   emit in2->changed( in2->metaProperty("instructionNumber") );
}

void Database::insertInstruction(Instruction* in, int pos)
{
   int parentRecipeKey;
   QSqlQuery q( QString("SELECT recipe_id FROM %1 WHERE %2=%3")
                   .arg(tableNames[INSTRUCTIONTABLE])
                   .arg(keyName(INSTRUCTIONTABLE))
                   .arg(in->_key),
                sqldb);
   q.next();
   parentRecipeKey = q.record().value("recipe_id").toInt();
   // Increment all instruction positions greater or equal to pos.
   sqlUpdate( tableNames[INSTRUCTIONTABLE],
              QString("instruction_number=instruction_number+1"),
              QString("recipe_id=%1 AND instruction_number>=%2")
                 .arg(parentRecipeKey)
                 .arg(pos) );
              
   // Change in's position to pos.
   sqlUpdate( tableNames[INSTRUCTIONTABLE],
              QString("instruction_number=%1").arg(pos),
              QString("%1=%2").arg(keyName(INSTRUCTIONTABLE)).arg(in->_key) );

   // emit changed
   emit in->changed( in->metaProperty("instructionNumber"), pos );
}

QList<BrewNote*> Database::brewNotes(Recipe const* parent)
{
   QList<BrewNote*> ret;
   QString queryString = QString("SELECT %1 FROM %2 WHERE recipe_id = %3")
                            .arg(keyName(BREWNOTETABLE))
                            .arg(tableNames[BREWNOTETABLE])
                            .arg(parent->_key);
   QSqlQuery q( queryString, sqldb );
   
   while( q.next() )
      ret.append(allBrewNotes[q.record().value(keyName(BREWNOTETABLE)).toInt()]);
   return ret;
}

QList<Fermentable*> Database::fermentables(Recipe const* parent)
{
   QList<Fermentable*> ret;
   QString queryString = QString("SELECT fermentable_id FROM fermentable_in_recipe WHERE recipe_id = %1").arg(parent->_key);
   QSqlQuery q( queryString, sqldb );
   
   while( q.next() )
      ret.append(allFermentables[q.record().value("fermentable_id").toInt()]);
   return ret;
}

QList<Hop*> Database::hops(Recipe const* parent)
{
   QList<Hop*> ret;
   QString queryString = QString("SELECT hop_id FROM hop_in_recipe WHERE recipe_id = %1").arg(parent->_key);
   QSqlQuery q( queryString, sqldb );
   
   while( q.next() )
      ret.append(allHops[q.record().value("hop_id").toInt()]);
   return ret;
}

QList<Misc*> Database::miscs(Recipe const* parent)
{
   QList<Misc*> ret;
   QString queryString = QString("SELECT misc_id FROM misc_in_recipe WHERE recipe_id = %1").arg(parent->_key);
   QSqlQuery q( queryString, sqldb );
   
   while( q.next() )
      ret.append(allMiscs[q.record().value("misc_id").toInt()]);
   return ret;
}

QList<MashStep*> Database::mashSteps(Mash const* parent)
{
   QList<MashStep*> ret;
   QString queryString = QString("SELECT %1 FROM %2 WHERE mash_id = %3")
                            .arg(keyName(MASHSTEPTABLE))
                            .arg(tableNames[MASHSTEPTABLE])
                            .arg(parent->_key);
   QSqlQuery q( queryString, sqldb );
   
   while( q.next() )
      ret.append(allMashSteps[q.record().value(keyName(MASHSTEPTABLE).toStdString().c_str()).toInt()]);
   return ret;
}

QList<Instruction*> Database::instructions( Recipe const* parent )
{
   QList<Instruction*> ret;
   QString queryString = QString("SELECT %1 FROM %2 WHERE recipe_id = %3 ORDER BY instruction_number ASC")
                            .arg(keyName(INSTRUCTIONTABLE))
                            .arg(tableNames[INSTRUCTIONTABLE])
                            .arg(parent->_key);
   QSqlQuery q( queryString, sqldb );
   
   while( q.next() )
      ret.append(allInstructions[q.record().value(keyName(INSTRUCTIONTABLE).toStdString().c_str()).toInt()]);
   return ret;
}

// TODO: implement all the rest of the BrewNote stuff.
/*
QList<BrewNote*> Database::brewNotes( Recipe* parent )
{
   QList<BrewNote*> ret;
   QString queryString = QString("SELECT %1 FROM %2 WHERE recipe_id = %3 ORDER BY brewDate ASC")
                            .arg(keyName(BREWNOTETABLE))
                            .arg(tableNames[BREWNOTETABLE])
                            .arg(parent->_key);
   QSqlQuery q( queryString, sqldb );
   
   while( q.next() )
      ret.append(allBrewNotes[q.record().value(keyName(BREWNOTETABLE).toStdString().c_str()).toInt()]);
   return ret;
}
*/

QList<Water*> Database::waters(Recipe const* parent)
{
   QList<Water*> ret;
   QString queryString = QString("SELECT water_id FROM water_in_recipe WHERE recipe_id = %1").arg(parent->_key);
   QSqlQuery q( queryString, sqldb );
   
   while( q.next() )
      ret.append(allWaters[q.record().value("water_id").toInt()]);
   return ret;
}

QList<Yeast*> Database::yeasts(Recipe const* parent)
{
   QList<Yeast*> ret;
   QString queryString = QString("SELECT yeast_id FROM yeast_in_recipe WHERE recipe_id = %1").arg(parent->_key);
   QSqlQuery q( queryString, sqldb );
   
   while( q.next() )
      ret.append(allYeasts[q.record().value("yeast_id").toInt()]);
   return ret;
}

// Named constructors =========================================================
int Database::insertNewRecord( DBTable table )
{
   // TODO: encapsulate this in a QUndoCommand so we can undo it.
   // TODO: implement default values with QSqlQuery::prepare().
   tableModel->setTable(tableNames[table]);
   tableModel->insertRecord(-1,tableModel->record());
   return tableModel->query().lastInsertId().toInt();
}

BrewNote* Database::newBrewNote(BrewNote* other)
{
   int newKey;
   BrewNote* tmp = new BrewNote();
   
   QSqlRecord r = copy(other);
   newKey = r.value(keyName(BREWNOTETABLE)).toInt();
   tmp->_key = newKey;
   tmp->_table = BREWNOTETABLE;
   allBrewNotes.insert( newKey, tmp );
   
   emit changed( metaProperty("brewNotes"), QVariant() );
   return tmp;
}

BrewNote* Database::newBrewNote(Recipe* parent)
{
   BrewNote* tmp = new BrewNote();
   tmp->_key = insertNewRecord(BREWNOTETABLE);
   tmp->_table = YEASTTABLE;
   allBrewNotes.insert(tmp->_key,tmp);
   sqlUpdate( tableNames[BREWNOTETABLE],
              QString("recipe_id=%1").arg(parent->_key),
              QString("%1=%2").arg(keyName(BREWNOTETABLE)).arg(tmp->_key) );
   emit changed( metaProperty("brewNotes"), QVariant() );
   return tmp;
}

Equipment* Database::newEquipment()
{
   Equipment* tmp = new Equipment();
   tmp->_key = insertNewRecord(EQUIPTABLE);
   tmp->_table = EQUIPTABLE;
   allEquipments.insert(tmp->_key,tmp);
   //emit changed( property("equipments"), allEquipments );
   emit changed( metaProperty("equipments"), QVariant() );
   return tmp;
}

Equipment* Database::newEquipment(Equipment* other)
{
   Equipment* tmp = new Equipment();
   tmp->_key = copy(other).value(keyName(EQUIPTABLE)).toInt();
   tmp->_table = EQUIPTABLE;
   allEquipments.insert(tmp->_key,tmp);
   //emit changed( property("equipments"), allEquipments );
   emit changed( metaProperty("equipments"), QVariant() );
   return tmp;
}

Fermentable* Database::newFermentable()
{
   Fermentable* tmp = new Fermentable();
   tmp->_key = insertNewRecord(FERMTABLE);
   tmp->_table = FERMTABLE;
   allFermentables.insert(tmp->_key,tmp);
   emit changed( metaProperty("fermentables"), QVariant() );
   return tmp;
}

Fermentable* Database::newFermentable(Fermentable* other)
{
   Fermentable* tmp = new Fermentable();
   tmp->_key = copy(other).value(keyName(FERMTABLE)).toInt();
   tmp->_table = FERMTABLE;
   allFermentables.insert(tmp->_key,tmp);
   emit changed( metaProperty("fermentables"), QVariant() );
   return tmp;
}

Hop* Database::newHop()
{
   Hop* tmp = new Hop();
   tmp->_key = insertNewRecord(HOPTABLE);
   tmp->_table = HOPTABLE;
   allHops.insert(tmp->_key,tmp);
   emit changed( metaProperty("hops"), QVariant() );
   return tmp;
}

Hop* Database::newHop(Hop* other)
{
   Hop* tmp = new Hop();
   tmp->_key = copy(other).value(keyName(HOPTABLE)).toInt();
   tmp->_table = HOPTABLE;
   allHops.insert(tmp->_key,tmp);
   emit changed( metaProperty("hops"), QVariant() );
   return tmp;
}

Instruction* Database::newInstruction(Recipe* rec)
{
   // TODO: encapsulate in QUndoCommand.
   // NOTE: we have unique(recipe_id,instruction_number) constraints on this table,
   // so may have to pay special attention when creating the new record.
   Instruction* tmp = new Instruction();
   tmp->_key = insertNewRecord(INSTRUCTIONTABLE);
   tmp->_table = INSTRUCTIONTABLE;
   QSqlQuery q( QString("SELECT * FROM instruction WHERE iid = %1").arg(tmp->_key),
                sqldb );
   q.next();
   q.record().setValue( "recipe_id", rec->_key );
   allInstructions.insert(tmp->_key,tmp);
   
   // Database's instructions have changed.
   emit changed( metaProperty("instructions"), QVariant() );
   // Recipe's instructions have changed.
   emit rec->changed( rec->metaProperty("instructions"), QVariant() );
   return tmp;
}

Mash* Database::newMash()
{
   Mash* tmp = new Mash();
   tmp->_key = insertNewRecord(MASHTABLE);
   tmp->_table = MASHTABLE;
   allMashs.insert(tmp->_key,tmp);
   emit changed( metaProperty("mashs"), QVariant() );
   return tmp;
}

Mash* Database::newMash(Recipe* parent)
{
   Mash* tmp = new Mash();
   tmp->_key = insertNewRecord(MASHTABLE);
   tmp->_table = MASHTABLE;
   allMashs.insert(tmp->_key,tmp);
   
   // Connect tmp to parent, removing any existing mash in parent.
   sqlUpdate( tableNames[RECTABLE],
              QString("mash_id=%1").arg(tmp->_key),
              QString("%1=%2").arg(keyName(RECTABLE)).arg(parent->_key) );
   
   emit changed( metaProperty("mashs"), QVariant() );
   return tmp;
}

Mash* Database::newMash(Mash* other, bool displace)
{
   Mash* tmp = new Mash();
   tmp->_key = copy(other).value(keyName(MASHTABLE)).toInt();
   tmp->_table = MASHTABLE;
   allMashs.insert(tmp->_key,tmp);
   
   // Connect tmp to parent, removing any existing mash in parent.
   if( displace )
   {
      sqlUpdate( tableNames[RECTABLE],
                 QString("mash_id=%1").arg(tmp->_key),
                 QString("mash_id=%1").arg(other->_key) );
   }
   
   emit changed( metaProperty("mashs"), QVariant() );
   return tmp;
}

MashStep* Database::newMashStep(Mash* mash)
{
   // TODO: encapsulate in QUndoCommand.
   // NOTE: we have unique(mash_id,step_number) constraints on this table,
   // so may have to pay special attention when creating the new record.
   MashStep* tmp = new MashStep();
   tmp->_key = insertNewRecord(MASHSTEPTABLE);
   tmp->_table = MASHSTEPTABLE;
   
   QSqlQuery q( QString("SELECT * FROM mashstep WHERE msid = %1").arg(tmp->_key),
                sqldb );
   q.next();
   q.record().setValue( "mash_id", mash->_key );
   
   allMashSteps.insert(tmp->_key,tmp);
   // Database's steps have changed.
   emit changed( metaProperty("mashSteps"), QVariant() );
   // Mash's steps have changed.
   emit mash->changed( mash->metaProperty("mashSteps"), QVariant() );
   return tmp;
}

Misc* Database::newMisc()
{
   Misc* tmp = new Misc();
   tmp->_key = insertNewRecord(MISCTABLE);
   tmp->_table = MISCTABLE;
   allMiscs.insert(tmp->_key,tmp);
   emit changed( metaProperty("miscs"), QVariant() );
   return tmp;
}

Misc* Database::newMisc(Misc* other)
{
   Misc* tmp = new Misc();
   tmp->_key = copy(other).value(keyName(MISCTABLE)).toInt();
   tmp->_table = MISCTABLE;
   allMiscs.insert(tmp->_key,tmp);
   emit changed( metaProperty("miscs"), QVariant() );
   return tmp;
}

Recipe* Database::newRecipe()
{
   Recipe* tmp = new Recipe();
   tmp->_key = insertNewRecord(RECTABLE);
   tmp->_table = RECTABLE;
   allRecipes.insert(tmp->_key,tmp);
   emit changed( metaProperty("recipes"), QVariant() );
   return tmp;
}

Recipe* Database::newRecipe(Recipe* other)
{
   Recipe* tmp = new Recipe();
   tmp->_key = copy(other).value(keyName(RECTABLE)).toInt();
   tmp->_table = RECTABLE;
   allRecipes.insert( tmp->_key, tmp );
   emit changed( metaProperty("recipes"), QVariant() );
   return tmp;
}

Style* Database::newStyle()
{
   Style* tmp = new Style();
   tmp->_key = insertNewRecord(STYLETABLE);
   tmp->_table = STYLETABLE;
   allStyles.insert(tmp->_key,tmp);
   emit changed( metaProperty("styles"), QVariant() );
   return tmp;
}

Water* Database::newWater()
{
   Water* tmp = new Water();
   tmp->_key = insertNewRecord(WATERTABLE);
   tmp->_table = WATERTABLE;
   allWaters.insert(tmp->_key,tmp);
   emit changed( metaProperty("waters"), QVariant() );
   return tmp;
}

Yeast* Database::newYeast()
{
   Yeast* tmp = new Yeast();
   tmp->_key = insertNewRecord(YEASTTABLE);
   tmp->_table = YEASTTABLE;
   allYeasts.insert(tmp->_key,tmp);
   emit changed( metaProperty("yeasts"), QVariant() );
   return tmp;
}

Yeast* Database::newYeast(Yeast* other)
{
   Yeast* tmp = new Yeast();
   tmp->_key = copy(other).value(keyName(YEASTTABLE)).toInt();
   tmp->_table = YEASTTABLE;
   allYeasts.insert(tmp->_key,tmp);
   emit changed( metaProperty("yeasts"), QVariant() );
   return tmp;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void Database::deleteRecord( DBTable table, BeerXMLElement* object )
{
   // Assumes the table has a column called 'deleted'.
   SetterCommand* command;
   command = new SetterCommand (tables[table],
                         keyName(table).toStdString().c_str(),
                         object->_key,
                         "deleted",
                         QVariant(true),
                         object->metaProperty("deleted"),
                         object,
                         true);
   // For now, immediately execute the command.
   command->redo();
   // Push the command on the undo stack.
   commandStack.push(command);
}

void Database::removeEquipment(Equipment* equip)
{
   deleteRecord(EQUIPTABLE,equip);
}

void Database::removeEquipment(QList<Equipment*> equip)
{
   QList<Equipment*>::Iterator it = equip.begin();
   while( it != equip.end() )
   {
      removeEquipment(*it);
      it++;
   }
}

void Database::removeFermentable(Fermentable* ferm)
{
   deleteRecord(FERMTABLE,ferm);
}

void Database::removeFermentable(QList<Fermentable*> ferm)
{
   QList<Fermentable*>::Iterator it = ferm.begin();
   while( it != ferm.end() )
   {
      removeFermentable(*it);
      it++;
   }
}

void Database::removeHop(Hop* hop)
{
   deleteRecord(HOPTABLE,hop);
}

void Database::removeHop(QList<Hop*> hop)
{
   QList<Hop*>::Iterator it = hop.begin();
   while( it != hop.end() )
   {
      removeHop(*it);
      it++;
   }
}

void Database::removeMash(Mash* mash)
{
   deleteRecord(MASHTABLE,mash);
}

void Database::removeMash(QList<Mash*> mash)
{
   QList<Mash*>::Iterator it = mash.begin();
   while( it != mash.end() )
   {
      removeMash(*it);
      it++;
   }
}

void Database::removeMashStep(MashStep* mashStep)
{
   deleteRecord(MASHSTEPTABLE,mashStep);
}

void Database::removeMashStep(QList<MashStep*> mashStep)
{
   QList<MashStep*>::Iterator it = mashStep.begin();
   while( it != mashStep.end() )
   {
      removeMashStep(*it);
      it++;
   }
}

void Database::removeMisc(Misc* misc)
{
   deleteRecord(MISCTABLE,misc);
}

void Database::removeMisc(QList<Misc*> misc)
{
   QList<Misc*>::Iterator it = misc.begin();
   while( it != misc.end() )
   {
      removeMisc(*it);
      it++;
   }
}

void Database::removeRecipe(Recipe* rec)
{
   deleteRecord(RECTABLE,rec);
}

void Database::removeRecipe(QList<Recipe*> rec)
{
   QList<Recipe*>::Iterator it = rec.begin();
   while( it != rec.end() )
   {
      removeRecipe(*it);
      it++;
   }
}

void Database::removeStyle(Style* style)
{
   deleteRecord(STYLETABLE,style);
}

void Database::removeStyle(QList<Style*> style)
{
   QList<Style*>::Iterator it = style.begin();
   while( it != style.end() )
   {
      removeStyle(*it);
      it++;
   }
}

void Database::removeWater(Water* water)
{
   deleteRecord(WATERTABLE,water);
}

void Database::removeWater(QList<Water*> water)
{
   QList<Water*>::Iterator it = water.begin();
   while( it != water.end() )
   {
      removeWater(*it);
      it++;
   }
}

void Database::removeYeast(Yeast* yeast)
{
   deleteRecord(YEASTTABLE,yeast);
}

void Database::removeYeast(QList<Yeast*> yeast)
{
   QList<Yeast*>::Iterator it = yeast.begin();
   while( it != yeast.end() )
   {
      removeYeast(*it);
      it++;
   }
}

QString Database::getDbFileName()
{
   // Ensure instance exists.
   instance();
   
   return dbFileName;
}

void Database::updateEntry( DBTable table, int key, const char* col_name, QVariant value, QMetaProperty prop, BeerXMLElement* object, bool notify )
{
   SetterCommand* command;
   command = new SetterCommand(tables[table],
                               keyName(table).toStdString().c_str(),
                               key,
                               col_name,
                               value,
                               prop,
                               object,
                               notify);
   // For now, immediately execute the command.
   command->redo();
   
   // Push the command on the undo stack.
   //commandStack.beginMacro("Change an entry");
   commandStack.push(command);
   //commandStack.endMacro();
}

QString Database::keyName( DBTable table )
{
   return tables[table]->primaryKey().name();
}

int Database::addIngredientToRecipe( Recipe* rec, BeerXMLElement* ing, QString propName, QString relTableName, QString ingKeyName )
{
   // TODO: encapsulate this in a QUndoCommand.
   int newKey;
   QSqlRecord r;
   tableModel->setTable(relTableName);
   
   // Ensure this ingredient is not already in the recipe.
   /*
   QSqlQuery q(
      QString("SELECT * from %1 WHERE %2 = %3 AND recipe_id = %4")
        .arg(relTableName).arg(ingKeyName).arg(ing->key).arg(rec->key), sqldb);
   if( q.next() )
   {
      Brewtarget::logW( "Ingredient already exists in recipe." );
      return;
   }
   */
   
   QString filter = tableModel->filter();
   
   // Ensure this ingredient is not already in the recipe.
   tableModel->setFilter(QString("%1=%2 AND recipe_id=%3").arg(ingKeyName).arg(ing->_key).arg(rec->_key));
   tableModel->select();
   if( tableModel->rowCount() > 0 )
   {
      Brewtarget::logW( "Ingredient already exists in recipe." );
      return -1;
   }
   
   // Create a copy of the ingredient.
   r = copy(ing);
   newKey = r.value(ingKeyName).toInt();
   
   // Reset the original filter.
   tableModel->setFilter(filter);
   tableModel->select();
   
   // Put this (rec,ing) pair in the <ing_type>_in_recipe table.
   r = tableModel->record(); // Should create a new record in that table.
   r.setValue(ingKeyName, newKey);
   r.setValue("recipe_id", rec->_key);
   tableModel->insertRecord(-1,r);
   
   emit rec->changed( rec->metaProperty(propName), QVariant() );
   return newKey;
}

QSqlRecord Database::copy( BeerXMLElement const* object )
{
   int newKey;
   int i;
   DBTable t = classNameToTable[object->metaObject()->className()];
   QString tName = tableNames[t];
   QSqlQuery q(QString("SELECT * FROM %1 WHERE %2 = %3")
               .arg(tName).arg(keyName(t)).arg(object->_key),
               sqldb
              );
   
   if( !q.next() )
      return QSqlRecord();
   
   QSqlRecord oldRecord = q.record();
   
   // Create a new row.
   newKey = insertNewRecord(t);
   q = QSqlQuery (QString("SELECT * FROM %1 WHERE %2 = %3")
                  .arg(tName).arg(keyName(t)).arg(object->_key),
                  sqldb
                 );
   q.next();
   QSqlRecord newRecord = q.record();
   
   // Set the new row's columns equal to the old one's, except for any "parent"
   // field, which should be set to the oldRecord's key.
   QString newValString;
   for( i = 0; i < oldRecord.count() - 1; ++i )
   {
      if( oldRecord.fieldName(i) != "parent" )
         newValString += QString("%1 = '%2',").arg(oldRecord.fieldName(i)).arg(oldRecord.value(i).toString());
      else
         newValString += QString("%1 = '%2',").arg(oldRecord.fieldName(i)).arg(object->_key);
   }
   if( oldRecord.fieldName(i) != "parent" )
      newValString += QString("%1 = %2").arg(oldRecord.fieldName(i)).arg(oldRecord.value(i).toString());
   else
      newValString += QString("%1 = '%2'").arg(oldRecord.fieldName(i)).arg(object->_key);
   
   QString updateString = QString("UPDATE %1 SET %2 WHERE %3 = %4")
                          .arg(tName)
                          .arg(newValString)
                          .arg(keyName(t))
                          .arg(newKey);
   q = QSqlQuery();
   q.prepare(updateString);
   q.exec();
   
   return newRecord;
}

// Add to recipe ==============================================================
void Database::addToRecipe( Recipe* rec, Hop* hop )
{
   addIngredientToRecipe( rec, hop, "hops", "hop_in_recipe", "hop_id" );
}

void Database::addToRecipe( Recipe* rec, Fermentable* ferm )
{
   addIngredientToRecipe( rec, ferm, "ferms", "fermentable_in_recipe", "fermentable_id" );
}

void Database::addToRecipe( Recipe* rec, Misc* m )
{
   addIngredientToRecipe( rec, m, "miscs", "misc_in_recipe", "misc_id" );
}

void Database::addToRecipe( Recipe* rec, Yeast* y )
{
   addIngredientToRecipe( rec, y, "yeasts", "yeast_in_recipe", "yeast_id" );
}

void Database::addToRecipe( Recipe* rec, Water* w )
{
   addIngredientToRecipe( rec, w, "waters", "water_in_recipe", "water_id" );
}

void Database::addToRecipe( Recipe* rec, Mash* m )
{
   // Make a copy of mash.
   QSqlRecord c = copy(m);
   
   // Update mash_id
   sqlUpdate(tableNames[RECTABLE],
             QString("mash_id=%1").arg(c.value(keyName(MASHTABLE)).toInt()),
             QString("%1=%2").arg(keyName(RECTABLE)).arg(rec->_key));
   /*
   QSqlQuery q( QString("UPDATE %1 SET %2=%3 WHERE %4=%5")
                .arg(tableNames[RECTABLE])
                .arg("mash_id")
                .arg(c.value("maid").toInt())
                .arg("rid")
                .arg(rec->_key),
                sqldb );
   */
   
   // Emit a changed signal.
   emit rec->changed( rec->metaProperty("mash"), QVariant() );
}

void Database::addToRecipe( Recipe* rec, Equipment* e )
{
   // Make a copy of equipment.
   QSqlRecord c = copy(e);
   
   // Update equipment_id
   sqlUpdate(tableNames[RECTABLE],
             QString("equipment_id=%1").arg(c.value(keyName(EQUIPTABLE)).toInt()),
             QString("%1=%2").arg(keyName(RECTABLE)).arg(rec->_key));

   // Emit a changed signal.
   emit rec->changed( rec->metaProperty("equipment"), QVariant() );
}

void Database::addToRecipe( Recipe* rec, Style* s )
{
   // Make a copy of style.
   QSqlRecord c = copy(s);
   
   // Update style_id
   sqlUpdate(tableNames[RECTABLE],
             QString("style_id=%1").arg(c.value(keyName(STYLETABLE)).toInt()),
             QString("%1=%2").arg(keyName(RECTABLE)).arg(rec->_key));

   // Emit a changed signal.
   emit rec->changed( rec->metaProperty("style"), QVariant() );
}
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void Database::sqlUpdate( QString const& tableName, QString const& setClause, QString const& whereClause )
{
   QSqlQuery q( QString("UPDATE %1 SET %2 WHERE %3")
                .arg(tableName)
                .arg(setClause)
                .arg(whereClause),
                sqldb );
}

void Database::sqlDelete( QString const& tableName, QString const& whereClause )
{
   QSqlQuery q( QString("DELETE FROM %1 WHERE %2")
                .arg(tableName)
                .arg(whereClause),
                sqldb );
}

void Database::getBrewNotes( QList<BrewNote*>& list, QString filter )
{
   getElements( list, filter, brewnotes_tm, BREWNOTETABLE, allBrewNotes );
}

void Database::getEquipments( QList<Equipment*>& list, QString filter )
{
   getElements( list, filter, equipments_tm, EQUIPTABLE, allEquipments );
}

void Database::getFermentables( QList<Fermentable*>& list, QString filter )
{
   getElements( list, filter, fermentables_tm, FERMTABLE, allFermentables );
}

void Database::getHops( QList<Hop*>& list, QString filter )
{
   getElements( list, filter, hops_tm, HOPTABLE, allHops );
}

void Database::getMashs( QList<Mash*>& list, QString filter )
{
   getElements( list, filter, mashs_tm, MASHTABLE, allMashs );
}

void Database::getMashSteps( QList<MashStep*>& list, QString filter )
{
   getElements( list, filter, mashSteps_tm, MASHSTEPTABLE, allMashSteps );
}

void Database::getMiscs( QList<Misc*>& list, QString filter )
{
   getElements( list, filter, miscs_tm, MISCTABLE, allMiscs );
}

void Database::getRecipes( QList<Recipe*>& list, QString filter )
{
   getElements( list, filter, recipes_tm, RECTABLE, allRecipes );
}

void Database::getStyles( QList<Style*>& list, QString filter )
{
   getElements( list, filter, styles_tm, STYLETABLE, allStyles );
}

void Database::getWaters( QList<Water*>& list, QString filter )
{
   getElements( list, filter, waters_tm, WATERTABLE, allWaters );
}

void Database::getYeasts( QList<Yeast*>& list, QString filter )
{
   getElements( list, filter, yeasts_tm, YEASTTABLE, allYeasts );
}

QHash<Database::DBTable,QString> Database::tableNamesHash()
{
   QHash<DBTable,QString> tmp;
   
   tmp[ BREWNOTETABLE ] = "brewnote";
   tmp[ EQUIPTABLE ] = "equipment";
   tmp[ FERMTABLE ] = "fermentable";
   tmp[ HOPTABLE ] = "hop";
   tmp[ INSTRUCTIONTABLE ] = "instruction";
   tmp[ MASHSTEPTABLE ] = "mashstep";
   tmp[ MASHTABLE ] = "mash";
   tmp[ MISCTABLE ] = "misc";
   tmp[ RECTABLE ] = "recipe";
   tmp[ STYLETABLE ] = "style";
   tmp[ WATERTABLE ] = "water";
   tmp[ YEASTTABLE ] = "yeast";
   
   return tmp;
}

QHash<QString,Database::DBTable> Database::classNameToTableHash()
{
   QHash<QString,DBTable> tmp;
   
   tmp["BrewNote"] = BREWNOTETABLE;
   tmp["Equipment"] = EQUIPTABLE;
   tmp["Fermentable"] = FERMTABLE;
   tmp["Hop"] = HOPTABLE;
   tmp["Instruction"] = INSTRUCTIONTABLE;
   tmp["MashStep"] = MASHSTEPTABLE;
   tmp["Mash"] = MASHTABLE;
   tmp["Misc"] = MISCTABLE;
   tmp["Recipe"] = RECTABLE;
   tmp["Style"] = STYLETABLE;
   tmp["Water"] = WATERTABLE;
   tmp["Yeast"] = YEASTTABLE;
   
   return tmp;
}

const QSqlRelationalTableModel* Database::getModel( DBTable table )
{
   return tables[table];
}

QList<BrewNote*>& Database::brewNotes()
{
   QList<BrewNote*>* tmp = new QList<BrewNote*>;
   getBrewNotes( *tmp, "deleted=FALSE" );
   return *tmp;
}

QList<Equipment*>& Database::equipments()
{
   QList<Equipment*>* tmp = new QList<Equipment*>;
   getEquipments( *tmp, "deleted=FALSE" );
   return *tmp;
}

QList<Fermentable*>& Database::fermentables()
{
   QList<Fermentable*>* tmp = new QList<Fermentable*>;
   getFermentables( *tmp, "deleted=FALSE" );
   return *tmp;
}

QList<Hop*>& Database::hops()
{
   QList<Hop*>* tmp = new QList<Hop*>;
   getHops( *tmp, "deleted=FALSE" );
   return *tmp;
}

QList<Mash*>& Database::mashs()
{
   QList<Mash*>* tmp = new QList<Mash*>;
   getMashs( *tmp, "deleted=FALSE" );
   return *tmp;
}

QList<MashStep*>& Database::mashSteps()
{
   QList<MashStep*>* tmp = new QList<MashStep*>;
   getMashSteps( *tmp, "deleted=FALSE" );
   return *tmp;
}

QList<Misc*>& Database::miscs()
{
   QList<Misc*>* tmp = new QList<Misc*>;
   getMiscs( *tmp, "deleted=FALSE" );
   return *tmp;
}

QList<Recipe*>& Database::recipes()
{
   QList<Recipe*>* tmp = new QList<Recipe*>;
   getRecipes( *tmp, "deleted=FALSE" );
   return *tmp;
}

QList<Style*>& Database::styles()
{
   QList<Style*>* tmp = new QList<Style*>;
   getStyles( *tmp, "deleted=FALSE" );
   return *tmp;
}

QList<Water*>& Database::waters()
{
   QList<Water*>* tmp = new QList<Water*>;
   getWaters( *tmp, "deleted=FALSE" );
   return *tmp;
}

QList<Yeast*>& Database::yeasts()
{
   QList<Yeast*>* tmp = new QList<Yeast*>;
   getYeasts( *tmp, "deleted=FALSE" );
   return *tmp;
}

void Database::importFromXML(const QString& filename)
{
   unsigned int count;
   int line, col;
   QDomDocument xmlDoc;
   QDomElement root;
   QDomNodeList list;
   QString err;
   QFile inFile;
   QStringList tags = QStringList() << "EQUIPMENT" << "HOP" << "MISC" << "YEAST";
   inFile.setFileName(filename);
   
   if( ! inFile.open(QIODevice::ReadOnly) )
   {
      Brewtarget::logW(QString("Database::importFromXML: Could not open %1 for reading.").arg(filename));
      return;
   }

   if( ! xmlDoc.setContent(&inFile, false, &err, &line, &col) )
      Brewtarget::logW(QString("Database::importFromXML: Bad document formatting in %1 %2:%3. %4").arg(filename).arg(line).arg(col).arg(err) );

   list = xmlDoc.elementsByTagName("RECIPE");
   if ( list.count() )
   {
      for(int i = 0; i < list.count(); ++i )
      {
         recipeFromXml( list.at(i) );
         //Recipe* newRec = new Recipe(list.at(i));
         //
         //if(verifyImport("recipe",newRec->getName()))
         //   db->addRecipe( newRec, true ); // Copy all subelements of the recipe into the db also.
      }
   }
   else
   {
      foreach (QString tag, tags)
      {
         list = xmlDoc.elementsByTagName(tag);
         count = list.size();

         if ( count > 0 ) 
         {
            // Tell how many there were in the status bar.
            //statusBar()->showMessage( tr("Found %1 %2.").arg(count).arg(tag.toLower()), 5000 );

            if (tag == "RECIPE")
            {
            }
            else if ( tag == "EQUIPMENT" )
            {
               for(int i = 0; i < list.count(); ++i )
                  equipmentFromXml( list.at(i) );
            }
            else if (tag == "HOP")
            {
               for(int i = 0; i < list.count(); ++i )
                  hopFromXml( list.at(i) );
            }
            else if (tag == "MISC")
            {
               for(int i = 0; i < list.count(); ++i )
                  miscFromXml( list.at(i) );
            }
            else if (tag == "YEAST")
            {
               for(int i = 0; i < list.count(); ++i )
                  yeastFromXml( list.at(i) );
            }
         }
      }
   }
}

void Database::toXml( BrewNote* a, QDomDocument& doc, QDomNode& parent )
{
   // TODO: implement
}

void Database::toXml( Equipment* a, QDomDocument& doc, QDomNode& parent )
{
   QDomElement equipNode;
   QDomElement tmpNode;
   QDomText tmpText;
   
   equipNode = doc.createElement("EQUIPMENT");
   
   tmpNode = doc.createElement("NAME");
   tmpText = doc.createTextNode(a->name());
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("VERSION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->version()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("BOIL_SIZE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->boilSize_l()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("BATCH_SIZE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->batchSize_l()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TUN_VOLUME");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->tunVolume_l()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TUN_WEIGHT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->tunWeight_kg()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TUN_SPECIFIC_HEAT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->tunSpecificHeat_calGC()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TOP_UP_WATER");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->topUpWater_l()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TRUB_CHILLER_LOSS");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->trubChillerLoss_l()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("EVAP_RATE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->evapRate_pctHr()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("REAL_EVAP_RATE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->evapRate_lHr()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);

   tmpNode = doc.createElement("BOIL_TIME");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->boilTime_min()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("CALC_BOIL_VOLUME");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->calcBoilVolume()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("LAUTER_DEADSPACE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->lauterDeadspace_l()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TOP_UP_KETTLE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->topUpKettle_l()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("HOP_UTILIZATION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->hopUtilization_pct()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("NOTES");
   tmpText = doc.createTextNode(a->notes());
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);

   // My extensions below
   tmpNode = doc.createElement("ABSORPTION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->grainAbsorption_LKg()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("BOILING_POINT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->boilingPoint_c()));
   tmpNode.appendChild(tmpText);
   equipNode.appendChild(tmpNode);
   parent.appendChild(equipNode);
}

void Database::toXml( Fermentable* a, QDomDocument& doc, QDomNode& parent )
{
   QDomElement fermNode;
   QDomElement tmpNode;
   QDomText tmpText;
   
   fermNode = doc.createElement("FERMENTABLE");
   
   tmpNode = doc.createElement("NAME");
   tmpText = doc.createTextNode(a->name());
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("VERSION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->version()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TYPE");
   tmpText = doc.createTextNode(Fermentable::types.at(a->type()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("AMOUNT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->amount_kg()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("YIELD");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->yield_pct()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("COLOR");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->color_srm()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("ADD_AFTER_BOIL");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->addAfterBoil()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("ORIGIN");
   tmpText = doc.createTextNode(a->origin());
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("SUPPLIER");
   tmpText = doc.createTextNode(a->supplier());
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("NOTES");
   tmpText = doc.createTextNode(a->notes());
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("COARSE_FINE_DIFF");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->coarseFineDiff_pct()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("MOISTURE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->moisture_pct()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("DIASTATIC_POWER");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->diastaticPower_lintner()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("PROTEIN");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->protein_pct()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("MAX_IN_BATCH");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->maxInBatch_pct()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("RECOMMEND_MASH");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->recommendMash()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("IS_MASHED");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->isMashed()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("IBU_GAL_PER_LB");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->ibuGalPerLb()));
   tmpNode.appendChild(tmpText);
   fermNode.appendChild(tmpNode);
   
   parent.appendChild(fermNode);
}

void Database::toXml( Hop* a, QDomDocument& doc, QDomNode& parent )
{
   QDomElement hopNode;
   QDomElement tmpNode;
   QDomText tmpText;
   
   hopNode = doc.createElement("HOP");
   
   tmpNode = doc.createElement("NAME");
   tmpText = doc.createTextNode(a->name());
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("VERSION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->version()));
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("ALPHA");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->alpha_pct()));
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("AMOUNT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->amount_kg()));
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("USE");
   tmpText = doc.createTextNode(a->useString());
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TIME");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->time_min()));
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("NOTES");
   tmpText = doc.createTextNode(a->notes());
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TYPE");
   tmpText = doc.createTextNode(a->typeString());
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("FORM");
   tmpText = doc.createTextNode(a->formString());
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("BETA");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->beta_pct()));
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("HSI");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->hsi_pct()));
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("ORIGIN");
   tmpText = doc.createTextNode(a->origin());
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("SUBSTITUTES");
   tmpText = doc.createTextNode(a->substitutes());
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("HUMULENE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->humulene_pct()));
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("CARYOPHYLLENE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->caryophyllene_pct()));
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("COHUMULONE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->cohumulone_pct()));
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("MYRCENE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->myrcene_pct()));
   tmpNode.appendChild(tmpText);
   hopNode.appendChild(tmpNode);
   
   parent.appendChild(hopNode);
}

void Database::toXml( Instruction* a, QDomDocument& doc, QDomNode& parent )
{
   QDomElement insNode;
   QDomElement tmpNode;
   QDomText tmpText;
   
   insNode = doc.createElement("INSTRUCTION");
   
   tmpNode = doc.createElement("NAME");
   tmpText = doc.createTextNode(a->name());
   tmpNode.appendChild(tmpText);
   insNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("DIRECTIONS");
   tmpText = doc.createTextNode(a->directions());
   tmpNode.appendChild(tmpText);
   insNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("HAS_TIMER");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->hasTimer()));
   tmpNode.appendChild(tmpText);
   insNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TIMER_VALUE");
   tmpText = doc.createTextNode(a->timerValue());
   tmpNode.appendChild(tmpText);
   insNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("COMPLETED");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->completed()));
   tmpNode.appendChild(tmpText);
   insNode.appendChild(tmpNode);

   tmpNode = doc.createElement("INTERVAL");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->interval()));
   tmpNode.appendChild(tmpText);
   insNode.appendChild(tmpNode);

   parent.appendChild(insNode);
}

void Database::toXml( Mash* a, QDomDocument& doc, QDomNode& parent )
{
   QDomElement mashNode;
   QDomElement tmpNode;
   QDomText tmpText;
   
   int i, size;
   
   mashNode = doc.createElement("MASH");
   
   tmpNode = doc.createElement("NAME");
   tmpText = doc.createTextNode(a->name());
   tmpNode.appendChild(tmpText);
   mashNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("VERSION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->version()));
   tmpNode.appendChild(tmpText);
   mashNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("GRAIN_TEMP");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->grainTemp_c()));
   tmpNode.appendChild(tmpText);
   mashNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("MASH_STEPS");
   QList<MashStep*> mashSteps = a->mashSteps();
   size = mashSteps.size();
   for( i = 0; i < size; ++i )
      toXml( mashSteps[i], doc, tmpNode);
   mashNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("NOTES");
   tmpText = doc.createTextNode(a->notes());
   tmpNode.appendChild(tmpText);
   mashNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TUN_TEMP");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->tunTemp_c()));
   tmpNode.appendChild(tmpText);
   mashNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("SPARGE_TEMP");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->spargeTemp_c()));
   tmpNode.appendChild(tmpText);
   mashNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("PH");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->ph()));
   tmpNode.appendChild(tmpText);
   mashNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TUN_WEIGHT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->tunWeight_kg()));
   tmpNode.appendChild(tmpText);
   mashNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TUN_SPECIFIC_HEAT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->tunSpecificHeat_calGC()));
   tmpNode.appendChild(tmpText);
   mashNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("EQUIP_ADJUST");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->equipAdjust()));
   tmpNode.appendChild(tmpText);
   mashNode.appendChild(tmpNode);
   
   parent.appendChild(mashNode);
}

void Database::toXml( MashStep* a, QDomDocument& doc, QDomNode& parent )
{
   QDomElement mashStepNode;
   QDomElement tmpNode;
   QDomText tmpText;
   
   mashStepNode = doc.createElement("MASH_STEP");
   
   tmpNode = doc.createElement("NAME");
   tmpText = doc.createTextNode(a->name());
   tmpNode.appendChild(tmpText);
   mashStepNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("VERSION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->version()));
   tmpNode.appendChild(tmpText);
   mashStepNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TYPE");
   tmpText = doc.createTextNode(a->typeString());
   tmpNode.appendChild(tmpText);
   mashStepNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("INFUSE_AMOUNT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->infuseAmount_l()));
   tmpNode.appendChild(tmpText);
   mashStepNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("STEP_TEMP");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->stepTemp_c()));
   tmpNode.appendChild(tmpText);
   mashStepNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("STEP_TIME");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->stepTime_min()));
   tmpNode.appendChild(tmpText);
   mashStepNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("RAMP_TIME");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->rampTime_min()));
   tmpNode.appendChild(tmpText);
   mashStepNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("END_TEMP");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->endTemp_c()));
   tmpNode.appendChild(tmpText);
   mashStepNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("INFUSE_TEMP");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->infuseTemp_c()));
   tmpNode.appendChild(tmpText);
   mashStepNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("DECOCTION_AMOUNT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->decoctionAmount_l()));
   tmpNode.appendChild(tmpText);
   mashStepNode.appendChild(tmpNode);
   
   parent.appendChild(mashStepNode);
}

void Database::toXml( Misc* a, QDomDocument& doc, QDomNode& parent )
{
   QDomElement miscNode;
   QDomElement tmpNode;
   QDomText tmpText;
   
   miscNode = doc.createElement("MISC");
   
   tmpNode = doc.createElement("NAME");
   tmpText = doc.createTextNode(a->name());
   tmpNode.appendChild(tmpText);
   miscNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("VERSION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->version()));
   tmpNode.appendChild(tmpText);
   miscNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TYPE");
   tmpText = doc.createTextNode(a->typeString());
   tmpNode.appendChild(tmpText);
   miscNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("USE");
   tmpText = doc.createTextNode(a->useString());
   tmpNode.appendChild(tmpText);
   miscNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TIME");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->time()));
   tmpNode.appendChild(tmpText);
   miscNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("AMOUNT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->amount()));
   tmpNode.appendChild(tmpText);
   miscNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("AMOUNT_IS_WEIGHT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->amountIsWeight()));
   tmpNode.appendChild(tmpText);
   miscNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("USE_FOR");
   tmpText = doc.createTextNode(a->useFor());
   tmpNode.appendChild(tmpText);
   miscNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("NOTES");
   tmpText = doc.createTextNode(a->notes());
   tmpNode.appendChild(tmpText);
   miscNode.appendChild(tmpNode);
   
   parent.appendChild(miscNode);
}

void Database::toXml( Recipe* a, QDomDocument& doc, QDomNode& parent )
{
   QDomElement recipeNode;
   QDomElement tmpNode;
   QDomText tmpText;
   
   int i;
   
   recipeNode = doc.createElement("RECIPE");
   
   tmpNode = doc.createElement("NAME");
   tmpText = doc.createTextNode(a->name());
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("VERSION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->version()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TYPE");
   tmpText = doc.createTextNode(a->type());
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   Style* style = a->style();
   if( style != 0 )
      toXml( style, doc, recipeNode);
   
   tmpNode = doc.createElement("BREWER");
   tmpText = doc.createTextNode(a->brewer());
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("BATCH_SIZE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->batchSize_l()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("BOIL_SIZE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->boilSize_l()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("BOIL_TIME");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->boilTime_min()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("EFFICIENCY");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->efficiency_pct()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("HOPS");
   QList<Hop*> hops = a->hops();
   for( i = 0; i < hops.size(); ++i )
      toXml( hops[i], doc, tmpNode);
   recipeNode.appendChild(tmpNode);

   tmpNode = doc.createElement("FERMENTABLES");
   QList<Fermentable*> ferms = a->fermentables();
   for( i = 0; i < ferms.size(); ++i )
      toXml( ferms[i], doc, tmpNode);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("MISCS");
   QList<Misc*> miscs = a->miscs();
   for( i = 0; i < miscs.size(); ++i )
      toXml( miscs[i], doc, tmpNode);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("YEASTS");
   QList<Yeast*> yeasts = a->yeasts();
   for( i = 0; i < yeasts.size(); ++i )
      toXml( yeasts[i], doc, tmpNode);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("WATERS");
   QList<Water*> waters = a->waters();
   for( i = 0; i < waters.size(); ++i )
      toXml( waters[i], doc, tmpNode);
   recipeNode.appendChild(tmpNode);
   
   Mash* mash = a->mash();
   if( mash != 0 )
      toXml( mash, doc, recipeNode);
   
   tmpNode = doc.createElement("INSTRUCTIONS");
   QList<Instruction*> instructions = a->instructions();
   for( i = 0; i < instructions.size(); ++i )
      toXml( instructions[i], doc, tmpNode);
   recipeNode.appendChild(tmpNode);

   tmpNode = doc.createElement("BREWNOTES");
   QList<BrewNote*> brewNotes = a->brewNotes();
   for(i=0; i < brewNotes.size(); ++i)
      toXml(brewNotes[i], doc, tmpNode);
   recipeNode.appendChild(tmpNode);

   tmpNode = doc.createElement("ASST_BREWER");
   tmpText = doc.createTextNode(a->asstBrewer());
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   Equipment* equip = a->equipment();
   if( equip )
      toXml( equip, doc, recipeNode);
   
   tmpNode = doc.createElement("NOTES");
   tmpText = doc.createTextNode(a->notes());
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TASTE_NOTES");
   tmpText = doc.createTextNode(a->tasteNotes());
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TASTE_RATING");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->tasteRating()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("OG");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->og()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("FG");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->fg()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("FERMENTATION_STAGES");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->fermentationStages()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("PRIMARY_AGE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->primaryAge_days()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("PRIMARY_TEMP");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->primaryTemp_c()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("SECONDARY_AGE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->secondaryAge_days()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("SECONDARY_TEMP");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->secondaryTemp_c()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TERTIARY_AGE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->tertiaryAge_days()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("TERTIARY_TEMP");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->tertiaryTemp_c()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("AGE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->age_days()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("AGE_TEMP");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->ageTemp_c()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("DATE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->date()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("CARBONATION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->carbonation_vols()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("FORCED_CARBONATION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->forcedCarbonation()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("PRIMING_SUGAR_NAME");
   tmpText = doc.createTextNode(a->primingSugarName());
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("CARBONATION_TEMP");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->carbonationTemp_c()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("PRIMING_SUGAR_EQUIV");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->primingSugarEquiv()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   tmpNode = doc.createElement("KEG_PRIMING_FACTOR");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->kegPrimingFactor()));
   tmpNode.appendChild(tmpText);
   recipeNode.appendChild(tmpNode);
   
   parent.appendChild(recipeNode);
}

void Database::toXml( Style* a, QDomDocument& doc, QDomNode& parent )
{
   QDomElement styleNode;
   QDomElement tmpNode;
   QDomText tmpText;

   styleNode = doc.createElement("STYLE");

   tmpNode = doc.createElement("NAME");
   tmpText = doc.createTextNode(a->name());
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("VERSION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->version()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("CATEGORY");
   tmpText = doc.createTextNode(a->category());
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("CATEGORY_NUMBER");
   tmpText = doc.createTextNode(a->categoryNumber());
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("STYLE_LETTER");
   tmpText = doc.createTextNode(a->styleLetter());
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("STYLE_GUIDE");
   tmpText = doc.createTextNode(a->styleGuide());
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("TYPE");
   tmpText = doc.createTextNode(a->typeString());
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("OG_MIN");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->ogMin()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("OG_MAX");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->ogMax()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("FG_MIN");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->fgMin()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("FG_MAX");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->fgMax()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("IBU_MIN");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->ibuMin()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("IBU_MAX");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->ibuMax()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("COLOR_MIN");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->colorMin_srm()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("COLOR_MAX");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->colorMax_srm()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("ABV_MIN");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->abvMin_pct()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("ABV_MAX");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->abvMax_pct()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("CARB_MIN");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->carbMin_vol()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("CARB_MAX");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->carbMax_vol()));
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("NOTES");
   tmpText = doc.createTextNode(a->notes());
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("PROFILE");
   tmpText = doc.createTextNode(a->profile());
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("INGREDIENTS");
   tmpText = doc.createTextNode(a->ingredients());
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   tmpNode = doc.createElement("EXAMPLES");
   tmpText = doc.createTextNode(a->examples());
   tmpNode.appendChild(tmpText);
   styleNode.appendChild(tmpNode);

   parent.appendChild(styleNode);
}

void Database::toXml( Water* a, QDomDocument& doc, QDomNode& parent )
{
   QDomElement waterNode;
   QDomElement tmpNode;
   QDomText tmpText;

   waterNode = doc.createElement("WATER");

   tmpNode = doc.createElement("NAME");
   tmpText = doc.createTextNode(a->name());
   tmpNode.appendChild(tmpText);
   waterNode.appendChild(tmpNode);

   tmpNode = doc.createElement("VERSION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->version()));
   tmpNode.appendChild(tmpText);
   waterNode.appendChild(tmpNode);

   tmpNode = doc.createElement("AMOUNT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->amount_l()));
   tmpNode.appendChild(tmpText);
   waterNode.appendChild(tmpNode);

   tmpNode = doc.createElement("CALCIUM");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->calcium_ppm()));
   tmpNode.appendChild(tmpText);
   waterNode.appendChild(tmpNode);

   tmpNode = doc.createElement("BICARBONATE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->bicarbonate_ppm()));
   tmpNode.appendChild(tmpText);
   waterNode.appendChild(tmpNode);

   tmpNode = doc.createElement("SULFATE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->sulfate_ppm()));
   tmpNode.appendChild(tmpText);
   waterNode.appendChild(tmpNode);

   tmpNode = doc.createElement("CHLORIDE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->chloride_ppm()));
   tmpNode.appendChild(tmpText);
   waterNode.appendChild(tmpNode);

   tmpNode = doc.createElement("SODIUM");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->sodium_ppm()));
   tmpNode.appendChild(tmpText);
   waterNode.appendChild(tmpNode);

   tmpNode = doc.createElement("MAGNESIUM");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->magnesium_ppm()));
   tmpNode.appendChild(tmpText);
   waterNode.appendChild(tmpNode);

   tmpNode = doc.createElement("PH");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->ph()));
   tmpNode.appendChild(tmpText);
   waterNode.appendChild(tmpNode);

   tmpNode = doc.createElement("NOTES");
   tmpText = doc.createTextNode(a->notes());
   tmpNode.appendChild(tmpText);
   waterNode.appendChild(tmpNode);

   parent.appendChild(waterNode);
}

void Database::toXml( Yeast* a, QDomDocument& doc, QDomNode& parent )
{
   QDomElement yeastNode;
   QDomElement tmpElement;
   QDomText tmpText;

   yeastNode = doc.createElement("YEAST");
   
   tmpElement = doc.createElement("NAME");
   tmpText = doc.createTextNode(a->name());
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);
   
   tmpElement = doc.createElement("VERSION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->version()));
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("TYPE");
   tmpText = doc.createTextNode(Yeast::types.at(a->type()));
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("FORM");
   tmpText = doc.createTextNode(Yeast::forms.at(a->form()));
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("AMOUNT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->amount()));
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("AMOUNT_IS_WEIGHT");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->amountIsWeight()));
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("LABORATORY");
   tmpText = doc.createTextNode(a->laboratory());
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("PRODUCT_ID");
   tmpText = doc.createTextNode(a->productID());
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("MIN_TEMPERATURE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->minTemperature_c()));
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("MAX_TEMPERATURE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->maxTemperature_c()));
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("FLOCCULATION");
   tmpText = doc.createTextNode(Yeast::flocculations.at(a->flocculation()));
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("ATTENUATION");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->attenuation_pct()));
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("NOTES");
   tmpText = doc.createTextNode(a->notes());
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("BEST_FOR");
   tmpText = doc.createTextNode(a->bestFor());
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("TIMES_CULTURED");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->timesCultured()));
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("MAX_REUSE");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->maxReuse()));
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   tmpElement = doc.createElement("ADD_TO_SECONDARY");
   tmpText = doc.createTextNode(BeerXMLElement::text(a->addToSecondary()));
   tmpElement.appendChild(tmpText);
   yeastNode.appendChild(tmpElement);

   parent.appendChild(yeastNode);
}

// fromXml ====================================================================
void Database::fromXml( BeerXMLElement* element, QHash<QString,QString> const& xmlTagsToProperties, QDomNode const& elementNode, bool showWarnings )
{
   QDomNode node, child;
   QDomText textNode;
   QString xmlTag;
   int intVal;
   double doubleVal;
   bool boolVal;
   QString stringVal;
   QDateTime dateTimeVal;
   
   for( node = elementNode.firstChild(); ! node.isNull(); node = node.nextSibling() )
   {
      if( ! node.isElement() )
      {
         Brewtarget::log(Brewtarget::WARNING, QString("Node at line %1 is not an element.").arg(textNode.lineNumber()) );
         continue;
      }
      
      child = node.firstChild();
      if( child.isNull() || ! child.isText() )
         continue;
      
      xmlTag = node.nodeName();
      textNode = child.toText();
      
      if( xmlTagsToProperties.contains(xmlTag) )
      {
         switch( element->metaProperty(xmlTagsToProperties[xmlTag]).type() )
         {
            case QVariant::Bool:
               boolVal = BeerXMLElement::getBool(textNode);
               element->setProperty(xmlTagsToProperties[xmlTag].toStdString().c_str(), boolVal);
               break;
            case QVariant::Double:
               doubleVal = BeerXMLElement::getDouble(textNode);
               element->setProperty(xmlTagsToProperties[xmlTag].toStdString().c_str(), doubleVal);
               break;
            case QVariant::Int:
               intVal = BeerXMLElement::getInt(textNode);
               element->setProperty(xmlTagsToProperties[xmlTag].toStdString().c_str(), intVal);
               break;
            case QVariant::DateTime:
               dateTimeVal = BeerXMLElement::getDateTime(textNode);
               element->setProperty(xmlTagsToProperties[xmlTag].toStdString().c_str(), dateTimeVal);
               break;
            // NOTE: I believe that enum types like Fermentable::Type will go
            // here since Q_ENUMS() converts enums to strings. So, need to make
            // sure that the enums match exactly what we expect in the XML.
            case QVariant::String:
               stringVal = BeerXMLElement::getString(textNode);
               element->setProperty(xmlTagsToProperties[xmlTag].toStdString().c_str(), stringVal);
               break;
            default:
               Brewtarget::logW("Database::fromXML: don't understand property type.");
               break;
         }
      }
      else
      {
         if( showWarnings )
            Brewtarget::logW(QString("Database::fromXML: Unsupported property: %1. Line %2").arg(xmlTag).arg(node.lineNumber()) );
      }
   }
}

BrewNote* Database::brewNoteFromXml( QDomNode const& node, Recipe* parent )
{
   QHash<QString,QString> propHash;
   BrewNote* ret = newBrewNote(parent);
   
   propHash["BREWDATE"] = "brewDate" ;
   propHash["DATE_FERMENTED_OUT"] = "fermentDate" ;
   propHash["SG"] = "sg" ;
   propHash["VOLUME_INTO_BK"] = "volumeIntoBK_l" ;
   propHash["STRIKE_TEMP"] = "strikeTemp_c" ;
   propHash["MASH_FINAL_TEMP"] = "mashFinTemp_c" ;
   propHash["OG"] = "og" ;
   propHash["POST_BOIL_VOLUME"] = "postBoilVolume_l" ;
   propHash["VOLUME_INTO_FERMENTER"] = "volumeIntoFerm_l" ;
   propHash["PITCH_TEMP"] = "pitchTemp_c" ;
   propHash["FG"] = "fg" ;
   propHash["EFF_INTO_BK"] = "effIntoBK_pct" ;
   propHash["PREDICTED_OG"] = "projOg" ;
   propHash["BREWHOUSE_EFF"] = "brewhouseEff_pct" ;
   //propHash["PREDICTED_ABV"] = "projABV_pct" ;
   propHash["ACTUAL_ABV"] = "abv" ;
   propHash["PROJECTED_BOIL_GRAV"] = "projBoilGrav" ;
   propHash["PROJECTED_STRIKE_TEMP"] = "projStrikeTemp_c" ;
   propHash["PROJECTED_MASH_FIN_TEMP"] = "projMashFinTemp_c" ;
   propHash["PROJECTED_VOL_INTO_BK"] = "projVolIntoBK_l" ;
   propHash["PROJECTED_OG"] = "projOg" ;
   propHash["PROJECTED_VOL_INTO_FERM"] = "projVolIntoFerm_l" ;
   propHash["PROJECTED_FG"] = "projFg" ;
   propHash["PROJECTED_EFF"] = "projEff_pct" ;
   propHash["PROJECTED_ABV"] = "projABV_pct" ;
   propHash["PROJECTED_POINTS"] = "projPoints" ;
   propHash["PROJECTED_ATTEN"] = "projAtten" ;
   propHash["BOIL_OFF"] = "boilOff_l" ;
   propHash["FINAL_VOLUME"] = "finalVolume_l" ;
   propHash["NOTES"] = "notes" ;
   
   fromXml( ret, propHash, node );
   return ret;
}

Equipment* Database::equipmentFromXml( QDomNode const& node, Recipe* parent )
{
   QHash<QString,QString> propHash;
   Equipment* ret = newEquipment();
   
   propHash["NAME"] = "name";
   propHash["BOIL_SIZE"] = "boilSize_l";
   propHash["BATCH_SIZE"] = "batchSize_l";
   propHash["TUN_VOLUME"] = "tunVolume_l";
   propHash["TUN_WEIGHT"] = "tunWeight_kg";
   propHash["TUN_SPECIFIC_HEAT"] = "tunSpecificHeat_calGC";
   propHash["TOP_UP_WATER"] = "topUpWater_l";
   propHash["TRUB_CHILLER_LOSS"] = "trubChillerLoss_l";
   propHash["EVAP_RATE"] = "evapRate_pctHr";
   propHash["REAL_EVAP_RATE"] = "evapRate_lHr";
   propHash["BOIL_TIME"] = "boilTime_min";
   propHash["CALC_BOIL_VOLUME"] = "calcBoilVolume";
   propHash["LAUTER_DEADSPACE"] = "lauterDeadspace_l";
   propHash["TOP_UP_KETTLE"] = "topUpKettle_l";
   propHash["HOP_UTILIZATION"] = "hopUtilization_pct";
   propHash["NOTES"] = "notes";
   propHash["ABSORPTION"] = "grainAbsorption_LKg";
   propHash["BOILING_POINT"] = "boilingPoint_c";
   
   fromXml( ret, propHash, node );
   if( parent )
      addToRecipe( parent, ret );
   return ret;
}

Fermentable* Database::fermentableFromXml( QDomNode const& node, Recipe* parent )
{
   QHash<QString,QString> propHash;
   Fermentable* ret = newFermentable();
   
   propHash["NAME"] = "name";
   propHash["TYPE"] = "type";
   propHash["AMOUNT"] = "amount_kg";
   propHash["YIELD"] = "yield_pct";
   propHash["COLOR"] = "color_srm";
   propHash["ADD_AFTER_BOIL"] = "addAfterBoil";
   propHash["ORIGIN"] = "origin";
   propHash["SUPPLIER"] = "supplier";
   propHash["NOTES"] = "notes";
   propHash["COARSE_FINE_DIFF"] = "coarseFineDiff_pct";
   propHash["MOISTURE"] = "moisture_pct";
   propHash["DIASTATIC_POWER"] = "diastaticPower_lintner";
   propHash["PROTEIN"] = "protein_pct";
   propHash["MAX_IN_BATCH"] = "maxInBatch_pct";
   propHash["RECOMMEND_MASH"] = "recommendMash";
   propHash["IS_MASHED"] = "isMashed";
   propHash["IBU_GAL_PER_LB"] = "ibuGalPerLb";
   
   fromXml( ret, propHash, node );
   if( parent )
      addToRecipe( parent, ret );
   return ret;
}

Hop* Database::hopFromXml( QDomNode const& node, Recipe* parent )
{
   QHash<QString,QString> propHash;
   Hop* ret = newHop();
   
   propHash["NAME"] = "name";
   propHash["ALPHA"] = "alpha_pct";
   propHash["AMOUNT"] = "amount_kg";
   propHash["USE"] = "use";
   propHash["TIME"] = "time_min";
   propHash["NOTES"] = "notes";
   propHash["TYPE"] = "type";
   propHash["FORM"] = "form";
   propHash["BETA"] = "beta_pct";
   propHash["HSI"] = "hsi_pct";
   propHash["ORIGIN"] = "origin";
   propHash["SUBSTITUTES"] = "substitutes";
   propHash["HUMULENE"] = "humulene_pct";
   propHash["CARYOPHYLLENE"] = "caryophyllene_pct";
   propHash["COHUMULONE"] = "cohumulone_pct";
   propHash["MYRCENE"] = "myrcene_pct";
   
   fromXml( ret, propHash, node );
   if( parent )
      addToRecipe( parent, ret );
   return ret;
}

Instruction* Database::instructionFromXml( QDomNode const& node, Recipe* parent )
{
   QHash<QString,QString> propHash;
   Instruction* ret = newInstruction(parent);
   
   propHash["NAME"] = "name";
   propHash["DIRECTIONS"] = "directions";
   propHash["HAS_TIMER"] = "hasTimer";
   propHash["TIMER_VALUE"] = "timerValue";
   propHash["COMPLETED"] = "completed";
   propHash["INTERVAL"] = "interval";
   
   fromXml( ret, propHash, node );
   return ret;
}

Mash* Database::mashFromXml( QDomNode const& node, Recipe* parent )
{
   QDomNode n;
   QHash<QString,QString> propHash;
   Mash* ret;
   if( parent )
      ret = newMash(parent);
   else
      ret = newMash();
   
   // First, get all the standard properties.
   propHash["NAME"] = "name";
   propHash["GRAIN_TEMP"] = "grainTemp_c";
   propHash["NOTES"] = "notes";
   propHash["TUN_TEMP"] = "tunTemp_c";
   propHash["SPARGE_TEMP"] = "spargeTemp_c";
   propHash["PH"] = "ph";
   propHash["TUN_WEIGHT"] = "tunWeight_kg";
   propHash["TUN_SPECIFIC_HEAT"] = "tunSpecificHeat_calGC";
   propHash["EQUIP_ADJUST"] = "equipAdjust";
   fromXml( ret, propHash, node );
   
   // Now, get the individual mash steps.
   n = node.firstChildElement("MASH_STEPS");
   if( n.isNull() )
      return ret;
   // Iterate through all the mash steps.
   for( n = n.firstChild(); !n.isNull(); n = n.nextSibling() )
      mashStepFromXml( n, ret );
   
   return ret;
}

MashStep* Database::mashStepFromXml( QDomNode const& node, Mash* parent )
{
   QHash<QString,QString> propHash;
   MashStep* ret = newMashStep(parent);
   
   propHash["NAME"] = "name";
   propHash["TYPE"] = "type";
   propHash["INFUSE_AMOUNT"] = "infuseAmount_l";
   propHash["STEP_TEMP"] = "stepTemp_c";
   propHash["STEP_TIME"] = "stepTime_min";
   propHash["RAMP_TIME"] = "rampTime_min";
   propHash["END_TEMP"] = "endTemp_c";
   propHash["INFUSE_TEMP"] = "infuseTemp_c";
   propHash["DECOCTION_AMOUNT"] = "decoctionAmount_l";
   
   fromXml( ret, propHash, node );
   return ret;
}

Misc* Database::miscFromXml( QDomNode const& node, Recipe* parent )
{
   QHash<QString,QString> propHash;
   Misc* ret = newMisc();
   
   propHash["NAME"] = "name";
   propHash["TYPE"] = "type";
   propHash["USE"] = "use";
   propHash["TIME"] = "time";
   propHash["AMOUNT"] = "amount";
   propHash["AMOUNT_IS_WEIGHT"] = "amountIsWeight";
   propHash["USE_FOR"] = "useFor";
   propHash["NOTES"] = "notes";
   
   fromXml( ret, propHash, node );
   if( parent )
      addToRecipe( parent, ret );
   return ret;
}

Recipe* Database::recipeFromXml( QDomNode const& node )
{
   QDomNode n;
   QHash<QString,QString> propHash;
   Recipe* ret = newRecipe();
   
   // First, get standard properties.
   propHash["NAME"] = "name";
   propHash["TYPE"] = "type";
   propHash["BREWER"] = "brewer";
   propHash["BATCH_SIZE"] = "batchSize_l";
   propHash["BOIL_SIZE"] = "boilSize_l";
   propHash["BOIL_TIME"] = "boilTime_min";
   propHash["EFFICIENCY"] = "efficiency_pct";
   propHash["ASST_BREWER"] = "asstBrewer";
   propHash["NOTES"] = "notes";
   propHash["TASTE_NOTES"] = "tasteNotes";
   propHash["TASTE_RATING"] = "tasteRating";
   propHash["OG"] = "og";
   propHash["FG"] = "fg";
   propHash["FERMENTATION_STAGES"] = "fermentationStages";
   propHash["PRIMARY_AGE"] = "primaryAge_days";
   propHash["PRIMARY_TEMP"] = "primaryTemp_c";
   propHash["SECONDARY_AGE"] = "secondaryAge_days";
   propHash["SECONDARY_TEMP"] = "secondaryTemp_c";
   propHash["TERTIARY_AGE"] = "tertiaryAge_days";
   propHash["TERTIARY_TEMP"] = "tertiaryTemp_c";
   propHash["AGE"] = "age_days";
   propHash["AGE_TEMP"] = "ageTemp_c";
   propHash["DATE"] = "date";
   propHash["CARBONATION"] = "carbonation_vols";
   propHash["FORCED_CARBONATION"] = "forcedCarbonation";
   propHash["PRIMING_SUGAR_NAME"] = "primingSugarName";
   propHash["CARBONATION_TEMP"] = "carbonationTemp_c";
   propHash["PRIMING_SUGAR_EQUIV"] = "primingSugarEquiv";
   propHash["KEG_PRIMING_FACTOR"] = "kegPrimingFactor";
   fromXml( ret, propHash, node );
   
   // Get style.
   n = node.firstChildElement("STYLE");
   styleFromXml(n.firstChild(), ret);
   
   // Get equipment.
   n = node.firstChildElement("EQUIPMENT");
   equipmentFromXml(n.firstChild(), ret);
   
   // Get hops.
   n = node.firstChildElement("HOPS");
   for( n = n.firstChild(); !n.isNull(); n = n.nextSibling() )
      hopFromXml(n, ret);
   
   // Get ferms.
   n = node.firstChildElement("FERMENTABLES");
   for( n = n.firstChild(); !n.isNull(); n = n.nextSibling() )
      fermentableFromXml(n, ret);
   
   // Get miscs.
   n = node.firstChildElement("MISCS");
   for( n = n.firstChild(); !n.isNull(); n = n.nextSibling() )
      miscFromXml(n, ret);
   
   // Get yeasts.
   n = node.firstChildElement("YEASTS");
   for( n = n.firstChild(); !n.isNull(); n = n.nextSibling() )
      yeastFromXml(n, ret);
   
   // Get waters.
   n = node.firstChildElement("WATERS");
   for( n = n.firstChild(); !n.isNull(); n = n.nextSibling() )
      waterFromXml(n, ret);
   
   // Get instructions.
   n = node.firstChildElement("INSTRUCTIONS");
   for( n = n.firstChild(); !n.isNull(); n = n.nextSibling() )
      instructionFromXml(n, ret);
   
   // Get brew notes
   n = node.firstChildElement("BREWNOTES");
   for( n = n.firstChild(); !n.isNull(); n = n.nextSibling() )
      brewNoteFromXml(n, ret);
   
   return ret;
}

Style* Database::styleFromXml( QDomNode const& node, Recipe* parent )
{
   QHash<QString,QString> propHash;
   Style* ret = newStyle();
   
   propHash["NAME"] = "name";
   propHash["CATEGORY"] = "category";
   propHash["CATEGORY_NUMBER"] = "categoryNumber";
   propHash["STYLE_LETTER"] = "styleLetter";
   propHash["STYLE_GUIDE"] = "styleGuide";
   propHash["TYPE"] = "type";
   propHash["OG_MIN"] = "ogMin";
   propHash["OG_MAX"] = "ogMax";
   propHash["FG_MIN"] = "fgMin";
   propHash["FG_MAX"] = "fgMax";
   propHash["IBU_MIN"] = "ibuMin";
   propHash["IBU_MAX"] = "ibuMax";
   propHash["COLOR_MIN"] = "colorMin_srm";
   propHash["COLOR_MAX"] = "colorMax_srm";
   propHash["CARB_MIN"] = "carbMin_vol";
   propHash["CARB_MAX"] = "carbMax_vol";
   propHash["ABV_MIN"] = "abvMin_pct";
   propHash["ABV_MAX"] = "abvMax_pct";
   propHash["NOTES"] = "notes";
   propHash["PROFILE"] = "profile";
   propHash["INGREDIENTS"] = "ingredients";
   propHash["EXAMPLES"] = "examples";
   
   fromXml( ret, propHash, node );
   if( parent )
      addToRecipe( parent, ret );
   return ret;
}

Water* Database::waterFromXml( QDomNode const& node, Recipe* parent )
{
   QHash<QString,QString> propHash;
   Water* ret = newWater();
   
   propHash["NAME"] = "name";
   propHash["AMOUNT"] = "amount_l";
   propHash["CALCIUM"] = "calcium_ppm";
   propHash["BICARBONATE"] = "bicarbonate_ppm";
   propHash["SULFATE"] = "sulfate_ppm";
   propHash["CHLORIDE"] = "chloride_ppm";
   propHash["SODIUM"] = "sodium_ppm";
   propHash["MAGNESIUM"] = "magnesium_ppm";
   propHash["PH"] = "ph";
   propHash["NOTES"] = "notes";
   
   fromXml( ret, propHash, node );
   if( parent )
      addToRecipe( parent, ret );
   return ret;
}

Yeast* Database::yeastFromXml( QDomNode const& node, Recipe* parent )
{
   QHash<QString,QString> propHash;
   Yeast* ret = newYeast();
   
   propHash["NAME"] = "name";
   propHash["TYPE"] = "type";
   propHash["FORM"] = "form";
   propHash["AMOUNT"] = "amount";
   propHash["AMOUNT_IS_WEIGHT"] = "amountIsWeight";
   propHash["LABORATORY"] = "laboratory";
   propHash["PRODUCT_ID"] = "productID";
   propHash["MIN_TEMPERATURE"] = "minTemperature_c";
   propHash["MAX_TEMPERATURE"] = "maxTemperature_c";
   propHash["FLOCCULATION"] = "flocculation";
   propHash["ATTENUATION"] = "attenuation_pct";
   propHash["NOTES"] = "notes";
   propHash["BEST_FOR"] = "bestFor";
   propHash["TIMES_CULTURED"] = "timesCultured";
   propHash["MAX_REUSE"] = "maxReuse";
   propHash["ADD_TO_SECONDARY"] = "addToSecondary";
   
   fromXml( ret, propHash, node );
   if( parent )
      addToRecipe( parent, ret );
   return ret;
}
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
