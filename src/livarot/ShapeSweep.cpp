/*
 *  ShapeSweep.cpp
 *  nlivarot
 *
 *  Created by fred on Thu Jun 19 2003.
 *
 */

#include "Shape.h"
#include "LivarotDefs.h"
//#include "MyMath.h"

#include "../libnr/nr-matrix.h"

/*
 * El Intersector.
 * algorithm: 1) benley ottman to get intersections of all the polygon's edges
 *            2) rounding of the points of the polygon, Hooby's algorithm
 *            3) DFS with clockwise choice of the edge to compute the windings
 *            4) choose edges according to winding numbers and fill rule
 * some additional nastyness: step 2 needs a seed winding number for the upper-left point of each 
 * connex subgraph of the graph. computing these brutally is O(n^3): baaaad. so during the sweeping in 1)
 * we keep for each point the edge of the resulting graph (not the original) that lies just on its left; 
 * when the time comes for the point to get its winding number computed, that edge must have been treated,
 * because its upper end lies above the aforementioned point, meaning we know the winding number of the point.
 * only, there is a catch: since we're sweeping the polygon, the edge we want to link the point to has not yet been
 * added (that would be too easy...). so the points are put on a linked list on the original shape's edge, and the list
 * is flushed when the edge is added.
 * rounding: to do the rounding, we need to find which edges cross the surrounding of the rounded points (at 
 * each sweepline position). grunt method tries all combination of "rounded points in the sweepline"x"edges crossing 
 * the sweepline". That's bad (and that's what polyboolean does, if i am not mistaken). so for each point 
 * rounded in a given sweepline, keep immediate left and right edges at the time the point is treated.
 * when edges/points crossing are searched, walk the edge list (in the  sweepline at the end of the batch) starting 
 * from the rounded points' left and right from that time. may sound strange, but it works because edges that
 * end or start in the batch have at least one end in the batch.
 * all these are the cause of the numerous linked lists of points and edges maintained in the sweeping
 */

void
Shape::ResetSweep (void)
{
  MakePointData (true);
  MakeEdgeData (true);
  MakeSweepSrcData (true);
}

void
Shape::CleanupSweep (void)
{
  MakePointData (false);
  MakeEdgeData (false);
  MakeSweepSrcData (false);
}

void
Shape::ForceToPolygon (void)
{
  type = shape_polygon;
}

int
Shape::Reoriente (Shape * a)
{
  Reset (0, 0);
  if (a->nbPt <= 1 || a->nbAr <= 1)
    return 0;
  if (a->Eulerian (true) == false)
    return shape_input_err;

  nbPt = a->nbPt;
  if (nbPt > maxPt)
    {
      maxPt = nbPt;
      pts = (dg_point *) realloc (pts, maxPt * sizeof (dg_point));
      if (HasPointsData ())
	pData = (point_data *) realloc (pData, maxPt * sizeof (point_data));
    }
  memcpy (pts, a->pts, nbPt * sizeof (dg_point));

  nbAr = a->nbAr;
  if (nbAr > maxAr)
    {
      maxAr = nbAr;
      aretes = (dg_arete *) realloc (aretes, maxAr * sizeof (dg_arete));
      if (HasEdgesData ())
	eData = (edge_data *) realloc (eData, maxPt * sizeof (edge_data));
      if (HasSweepSrcData ())
	swsData =
	  (sweep_src_data *) realloc (swsData,
				      maxAr * sizeof (sweep_src_data));
      if (HasSweepDestData ())
	swdData =
	  (sweep_dest_data *) realloc (swdData,
				       maxAr * sizeof (sweep_dest_data));
      if (HasRasterData ())
	swrData =
	  (raster_data *) realloc (swrData, maxAr * sizeof (raster_data));
    }
  memcpy (aretes, a->aretes, nbAr * sizeof (dg_arete));

  MakePointData (true);
  MakeEdgeData (true);
  MakeSweepDestData (true);

  for (int i = 0; i < nbPt; i++)
    {
      pData[i].pending = 0;
      pData[i].edgeOnLeft = -1;
      pData[i].nextLinkedPoint = -1;
      pData[i].rx[0] = Round (pts[i].x[0]);
      pData[i].rx[1] = Round (pts[i].x[1]);
      pts[i].x = pData[i].rx;
    }
  for (int i = 0; i < nbPt; i++)
    {
      pts[i].oldDegree = pts[i].dI + pts[i].dO;
    }
  for (int i = 0; i < a->nbAr; i++)
    {
      eData[i].rdx = pData[aretes[i].en].rx - pData[aretes[i].st].rx;
      eData[i].weight = 1;
      aretes[i].dx = eData[i].rdx;
    }

  SortPointsRounded ();

  SetFlag (need_edges_sorting, true);
  GetWindings (this, NULL, bool_op_union, true);

//      Plot(341,56,8,400,400,true,true,false,true);
  for (int i = 0; i < nbAr; i++)
    {
      swdData[i].leW %= 2;
      swdData[i].riW %= 2;
      if (swdData[i].leW < 0)
	swdData[i].leW = -swdData[i].leW;
      if (swdData[i].riW < 0)
	swdData[i].riW = -swdData[i].riW;
      if (swdData[i].leW > 0 && swdData[i].riW <= 0)
	{
	  eData[i].weight = 1;
	}
      else if (swdData[i].leW <= 0 && swdData[i].riW > 0)
	{
	  Inverse (i);
	  eData[i].weight = 1;
	}
      else
	{
	  eData[i].weight = 0;
	  SubEdge (i);
	  i--;
	}
    }

  MakePointData (false);
  MakeEdgeData (false);
  MakeSweepDestData (false);

  if (Eulerian (true) == false)
    {
//              printf( "pas euclidian2");
      nbPt = nbAr = 0;
      return shape_euler_err;
    }

  type = shape_polygon;
  return 0;
}

int
Shape::ConvertToShape (Shape * a, FillRule directed, bool invert)
{
  Reset (0, 0);
  if (a->nbPt <= 1 || a->nbAr <= 1)
    return 0;
  if ( directed == fill_justDont ) {
  } else {
    if (a->Eulerian (true) == false)
      return shape_input_err;
  }
  
  a->ResetSweep ();

  if (GetFlag (has_sweep_data))
    {
    }
  else
    {
      SweepTree::CreateList (sTree, a->nbAr);
      SweepEvent::CreateQueue (sEvts, a->nbAr);
      SetFlag (has_sweep_data, true);
    }
  MakePointData (true);
  MakeEdgeData (true);
  MakeSweepSrcData (true);
  MakeSweepDestData (true);
  if (a->HasBackData ())
    {
      MakeBackData (true);
    }
  else
    {
      MakeBackData (false);
    }

  for (int i = 0; i < a->nbPt; i++)
    {
      a->pData[i].pending = 0;
      a->pData[i].edgeOnLeft = -1;
      a->pData[i].nextLinkedPoint = -1;
      a->pData[i].rx[0] = Round (a->pts[i].x[0]);
      a->pData[i].rx[1] = Round (a->pts[i].x[1]);
    }
  for (int i = 0; i < a->nbAr; i++)
    {
      a->eData[i].rdx =
	a->pData[a->aretes[i].en].rx - a->pData[a->aretes[i].st].rx;
    a->eData[i].length = dot(a->eData[i].rdx,a->eData[i].rdx);
      a->eData[i].ilength = 1 / a->eData[i].length;
      a->eData[i].sqlength = sqrt (a->eData[i].length);
      a->eData[i].isqlength = 1 / a->eData[i].sqlength;
      a->eData[i].siEd = a->eData[i].rdx[1] * a->eData[i].isqlength;
      a->eData[i].coEd = a->eData[i].rdx[0] * a->eData[i].isqlength;
      if (a->eData[i].siEd < 0)
	{
	  a->eData[i].siEd = -a->eData[i].siEd;
	  a->eData[i].coEd = -a->eData[i].coEd;
	}

      a->swsData[i].misc = NULL;
      a->swsData[i].firstLinkedPoint = -1;
      a->swsData[i].stPt = a->swsData[i].enPt = -1;
      a->swsData[i].leftRnd = a->swsData[i].rightRnd = -1;
      a->swsData[i].nextSh = NULL;
      a->swsData[i].nextBo = -1;
      a->swsData[i].curPoint = -1;
      a->swsData[i].doneTo = -1;
    }

  a->SortPointsRounded ();

//      a->Plot(200.0,200.0,2.0,400.0,400.0,true,true,true,true);

  chgts = NULL;
  nbChgt = maxChgt = 0;

  double lastChange = a->pData[0].rx[1] - 1.0;
  int lastChgtPt = 0;
  int edgeHead = -1;
  Shape *shapeHead = NULL;

  iData = NULL;
  nbInc = maxInc = 0;

  int curAPt = 0;

  while (curAPt < a->nbPt || sEvts.nbEvt > 0)
    {
/*		if ( nbPt > 0 && pts[nbPt-1].y >= 250.4 && pts[nbPt-1].y <= 250.6 ) {
			for (int i=0;i<sEvts.nbEvt;i++) {
				printf("%f %f %i %i\n",sEvts.events[i].posx,sEvts.events[i].posy,sEvts.events[i].leftSweep->bord,sEvts.events[i].rightSweep->bord);
			}
			//		cout << endl;
			if ( sTree.racine ) {
				SweepTree*  ct=static_cast <SweepTree*> (sTree.racine->Leftmost());
				while ( ct ) {
					printf("%i %i\n",ct->bord,ct->startPoint);
					ct=static_cast <SweepTree*> (ct->rightElem);
				}
			}
		}*/
//              cout << endl << endl;

    NR::Point ptX;
      double ptL, ptR;
      SweepTree *intersL = NULL;
      SweepTree *intersR = NULL;
      int nPt = -1;
      Shape *ptSh = NULL;
      bool isIntersection = false;
      if (SweepEvent::
	  PeekInQueue (intersL, intersR, ptX, ptL, ptR, sEvts))
	{
	  if (a->pData[curAPt].pending > 0
	      || (a->pData[curAPt].rx[1] > ptX[1]
		  || (a->pData[curAPt].rx[1] == ptX[1]
		      && a->pData[curAPt].rx[0] > ptX[0])))
	    {
	      SweepEvent::ExtractFromQueue (intersL, intersR, ptX, ptL,
					    ptR, sEvts);
	      isIntersection = true;
	    }
	  else
	    {
	      nPt = curAPt++;
	      ptSh = a;
	      ptX = ptSh->pData[nPt].rx;
	      isIntersection = false;
	    }
	}
      else
	{
	  nPt = curAPt++;
	  ptSh = a;
	  ptX = ptSh->pData[nPt].rx;
	  isIntersection = false;
	}

      if (isIntersection == false)
	{
	  if (ptSh->pts[nPt].dI == 0 && ptSh->pts[nPt].dO == 0)
	    continue;
	}

      NR::Point rPtX;
      rPtX[0]= Round (ptX[0]);
      rPtX[1]= Round (ptX[1]);
      int lastPointNo = -1;
      lastPointNo = AddPoint (rPtX);
      pData[lastPointNo].rx = rPtX;

      if (rPtX[1] > lastChange)
	{
	  int lastI = AssemblePoints (lastChgtPt, lastPointNo);

	  for (int i = lastChgtPt; i < lastI; i++)
	    {
	      if (pData[i].askForWindingS)
		{
		  Shape *windS = pData[i].askForWindingS;
		  int windB = pData[i].askForWindingB;
		  pData[i].nextLinkedPoint =
		    windS->swsData[windB].firstLinkedPoint;
		  windS->swsData[windB].firstLinkedPoint = i;
		}
	    }

	  Shape *curSh = shapeHead;
	  int curBo = edgeHead;
	  while (curSh)
	    {
	      curSh->swsData[curBo].leftRnd =
		pData[curSh->swsData[curBo].leftRnd].newInd;
	      curSh->swsData[curBo].rightRnd =
		pData[curSh->swsData[curBo].rightRnd].newInd;

	      Shape *neSh = curSh->swsData[curBo].nextSh;
	      curBo = curSh->swsData[curBo].nextBo;
	      curSh = neSh;
	    }

	  for (int i = 0; i < nbChgt; i++)
	    {
	      chgts[i].ptNo = pData[chgts[i].ptNo].newInd;
	      if (chgts[i].type == 0)
		{
		  if (chgts[i].src->aretes[chgts[i].bord].st <
		      chgts[i].src->aretes[chgts[i].bord].en)
		    {
		      chgts[i].src->swsData[chgts[i].bord].stPt =
			chgts[i].ptNo;
		    }
		  else
		    {
		      chgts[i].src->swsData[chgts[i].bord].enPt =
			chgts[i].ptNo;
		    }
		}
	      else if (chgts[i].type == 1)
		{
		  if (chgts[i].src->aretes[chgts[i].bord].st >
		      chgts[i].src->aretes[chgts[i].bord].en)
		    {
		      chgts[i].src->swsData[chgts[i].bord].stPt =
			chgts[i].ptNo;
		    }
		  else
		    {
		      chgts[i].src->swsData[chgts[i].bord].enPt =
			chgts[i].ptNo;
		    }
		}
	    }

	  CheckAdjacencies (lastI, lastChgtPt, shapeHead, edgeHead);

	  CheckEdges (lastI, lastChgtPt, a, NULL, bool_op_union);

	  if (lastI < lastPointNo)
	    {
	      pts[lastI] = pts[lastPointNo];
	      pData[lastI] = pData[lastPointNo];
	    }
	  lastPointNo = lastI;
	  nbPt = lastI + 1;

	  lastChgtPt = lastPointNo;
	  lastChange = rPtX[1];
	  nbChgt = 0;
	  edgeHead = -1;
	  shapeHead = NULL;
	}


      if (isIntersection)
	{
//                      printf("(%i %i [%i %i]) ",intersL->bord,intersR->bord,intersL->startPoint,intersR->startPoint);
	  intersL->RemoveEvent (sEvts, true);
	  intersR->RemoveEvent (sEvts, false);

	  AddChgt (lastPointNo, lastChgtPt, shapeHead, edgeHead, 2,
		   intersL->src, intersL->bord, intersR->src, intersR->bord);

	  intersL->SwapWithRight (sTree, sEvts);

	  TesteIntersection (intersL, true, false);
	  TesteIntersection (intersR, false, false);
	}
      else
	{
	  int cb;

	  int nbUp = 0, nbDn = 0;
	  int upNo = -1, dnNo = -1;
	  cb = ptSh->pts[nPt].firstA;
	  while (cb >= 0 && cb < ptSh->nbAr)
	    {
	      if ((ptSh->aretes[cb].st < ptSh->aretes[cb].en
		   && nPt == ptSh->aretes[cb].en)
		  || (ptSh->aretes[cb].st > ptSh->aretes[cb].en
		      && nPt == ptSh->aretes[cb].st))
		{
		  upNo = cb;
		  nbUp++;
		}
	      if ((ptSh->aretes[cb].st > ptSh->aretes[cb].en
		   && nPt == ptSh->aretes[cb].en)
		  || (ptSh->aretes[cb].st < ptSh->aretes[cb].en
		      && nPt == ptSh->aretes[cb].st))
		{
		  dnNo = cb;
		  nbDn++;
		}
	      cb = ptSh->NextAt (nPt, cb);
	    }

	  if (nbDn <= 0)
	    {
	      upNo = -1;
	    }
	  if (upNo >= 0 && (SweepTree *) ptSh->swsData[upNo].misc == NULL)
	    {
	      upNo = -1;
	    }

	  bool doWinding = true;

	  if (nbUp > 0)
	    {
	      cb = ptSh->pts[nPt].firstA;
	      while (cb >= 0 && cb < ptSh->nbAr)
		{
		  if ((ptSh->aretes[cb].st < ptSh->aretes[cb].en
		       && nPt == ptSh->aretes[cb].en)
		      || (ptSh->aretes[cb].st > ptSh->aretes[cb].en
			  && nPt == ptSh->aretes[cb].st))
		    {
		      if (cb != upNo)
			{
			  SweepTree *node =
			    (SweepTree *) ptSh->swsData[cb].misc;
			  if (node == NULL)
			    {
			    }
			  else
			    {
			      AddChgt (lastPointNo, lastChgtPt, shapeHead,
				       edgeHead, 1, node->src, node->bord,
				       NULL, -1);
			      ptSh->swsData[cb].misc = NULL;

			      int onLeftB = -1, onRightB = -1;
			      Shape *onLeftS = NULL;
			      Shape *onRightS = NULL;
			      if (node->leftElem)
				{
				  onLeftB =
				    (static_cast <
				     SweepTree * >(node->leftElem))->bord;
				  onLeftS =
				    (static_cast <
				     SweepTree * >(node->leftElem))->src;
				}
			      if (node->rightElem)
				{
				  onRightB =
				    (static_cast <
				     SweepTree * >(node->rightElem))->bord;
				  onRightS =
				    (static_cast <
				     SweepTree * >(node->rightElem))->src;
				}

			      node->Remove (sTree, sEvts, true);
			      if (onLeftS && onRightS)
				{
				  SweepTree *onLeft =
				    (SweepTree *) onLeftS->swsData[onLeftB].
				    misc;
				  if (onLeftS == ptSh
				      && (onLeftS->aretes[onLeftB].en == nPt
					  || onLeftS->aretes[onLeftB].st ==
					  nPt))
				    {
				    }
				  else
				    {
				      if (onRightS == ptSh
					  && (onRightS->aretes[onRightB].en ==
					      nPt
					      || onRightS->aretes[onRightB].
					      st == nPt))
					{
					}
				      else
					{
					  TesteIntersection (onLeft, false,
							     false);
					}
				    }
				}
			    }
			}
		    }
		  cb = ptSh->NextAt (nPt, cb);
		}
	    }

	  // traitement du "upNo devient dnNo"
	  SweepTree *insertionNode = NULL;
	  if (dnNo >= 0)
	    {
	      if (upNo >= 0)
		{
		  SweepTree *node = (SweepTree *) ptSh->swsData[upNo].misc;

		  AddChgt (lastPointNo, lastChgtPt, shapeHead, edgeHead, 1,
			   node->src, node->bord, NULL, -1);

		  ptSh->swsData[upNo].misc = NULL;

		  node->RemoveEvents (sEvts);
		  node->ConvertTo (ptSh, dnNo, 1, lastPointNo);
		  ptSh->swsData[dnNo].misc = node;
		  TesteIntersection (node, false, false);
		  TesteIntersection (node, true, false);
		  insertionNode = node;

		  ptSh->swsData[dnNo].curPoint = lastPointNo;
		  AddChgt (lastPointNo, lastChgtPt, shapeHead, edgeHead, 0,
			   node->src, node->bord, NULL, -1);
		}
	      else
		{
		  SweepTree *node =
		    SweepTree::AddInList (ptSh, dnNo, 1, lastPointNo, sTree,
					  this);
		  ptSh->swsData[dnNo].misc = node;
		  node->Insert (sTree, sEvts, this, lastPointNo, true);
		  if (doWinding)
		    {
		      SweepTree *myLeft =
			static_cast < SweepTree * >(node->leftElem);
		      if (myLeft)
			{
			  pData[lastPointNo].askForWindingS = myLeft->src;
			  pData[lastPointNo].askForWindingB = myLeft->bord;
			}
		      else
			{
			  pData[lastPointNo].askForWindingB = -1;
			}
		      doWinding = false;
		    }
		  TesteIntersection (node, false, false);
		  TesteIntersection (node, true, false);
		  insertionNode = node;

		  ptSh->swsData[dnNo].curPoint = lastPointNo;
		  AddChgt (lastPointNo, lastChgtPt, shapeHead, edgeHead, 0,
			   node->src, node->bord, NULL, -1);
		}
	    }

	  if (nbDn > 1)
	    {			// si nbDn == 1 , alors dnNo a deja ete traite
	      cb = ptSh->pts[nPt].firstA;
	      while (cb >= 0 && cb < ptSh->nbAr)
		{
		  if ((ptSh->aretes[cb].st > ptSh->aretes[cb].en
		       && nPt == ptSh->aretes[cb].en)
		      || (ptSh->aretes[cb].st < ptSh->aretes[cb].en
			  && nPt == ptSh->aretes[cb].st))
		    {
		      if (cb != dnNo)
			{
			  SweepTree *node =
			    SweepTree::AddInList (ptSh, cb, 1, lastPointNo,
						  sTree, this);
			  ptSh->swsData[cb].misc = node;
			  node->InsertAt (sTree, sEvts, this, insertionNode,
					  nPt, true);
			  if (doWinding)
			    {
			      SweepTree *myLeft =
				static_cast < SweepTree * >(node->leftElem);
			      if (myLeft)
				{
				  pData[lastPointNo].askForWindingS =
				    myLeft->src;
				  pData[lastPointNo].askForWindingB =
				    myLeft->bord;
				}
			      else
				{
				  pData[lastPointNo].askForWindingB = -1;
				}
			      doWinding = false;
			    }
			  TesteIntersection (node, false, false);
			  TesteIntersection (node, true, false);

			  ptSh->swsData[cb].curPoint = lastPointNo;
			  AddChgt (lastPointNo, lastChgtPt, shapeHead,
				   edgeHead, 0, node->src, node->bord, NULL,
				   -1);
			}
		    }
		  cb = ptSh->NextAt (nPt, cb);
		}
	    }
	}
    }
  {
    int lastI = AssemblePoints (lastChgtPt, nbPt);

    for (int i = lastChgtPt; i < lastI; i++)
      {
	if (pData[i].askForWindingS)
	  {
	    Shape *windS = pData[i].askForWindingS;
	    int windB = pData[i].askForWindingB;
	    pData[i].nextLinkedPoint = windS->swsData[windB].firstLinkedPoint;
	    windS->swsData[windB].firstLinkedPoint = i;
	  }
      }

    Shape *curSh = shapeHead;
    int curBo = edgeHead;
    while (curSh)
      {
	curSh->swsData[curBo].leftRnd =
	  pData[curSh->swsData[curBo].leftRnd].newInd;
	curSh->swsData[curBo].rightRnd =
	  pData[curSh->swsData[curBo].rightRnd].newInd;

	Shape *neSh = curSh->swsData[curBo].nextSh;
	curBo = curSh->swsData[curBo].nextBo;
	curSh = neSh;
      }

    for (int i = 0; i < nbChgt; i++)
      {
	chgts[i].ptNo = pData[chgts[i].ptNo].newInd;
	if (chgts[i].type == 0)
	  {
	    if (chgts[i].src->aretes[chgts[i].bord].st <
		chgts[i].src->aretes[chgts[i].bord].en)
	      {
		chgts[i].src->swsData[chgts[i].bord].stPt = chgts[i].ptNo;
	      }
	    else
	      {
		chgts[i].src->swsData[chgts[i].bord].enPt = chgts[i].ptNo;
	      }
	  }
	else if (chgts[i].type == 1)
	  {
	    if (chgts[i].src->aretes[chgts[i].bord].st >
		chgts[i].src->aretes[chgts[i].bord].en)
	      {
		chgts[i].src->swsData[chgts[i].bord].stPt = chgts[i].ptNo;
	      }
	    else
	      {
		chgts[i].src->swsData[chgts[i].bord].enPt = chgts[i].ptNo;
	      }
	  }
      }

    CheckAdjacencies (lastI, lastChgtPt, shapeHead, edgeHead);

    CheckEdges (lastI, lastChgtPt, a, NULL, bool_op_union);

    nbPt = lastI;

    edgeHead = -1;
    shapeHead = NULL;
  }

  if (chgts)
    free (chgts);
  chgts = NULL;
  nbChgt = maxChgt = 0;

//  Plot (98.0, 112.0, 8.0, 400.0, 400.0, true, true, true, true);
//      Plot(200.0,200.0,2.0,400.0,400.0,true,true,true,true);

  //      AssemblePoints(a);

//      GetAdjacencies(a);

//      MakeAretes(a);
  if (iData)
    free (iData);
  iData = NULL;
  nbInc = maxInc = 0;

  AssembleAretes (directed);

//  Plot (98.0, 112.0, 8.0, 400.0, 400.0, true, true, true, true);

  for (int i = 0; i < nbPt; i++)
    {
      pts[i].oldDegree = pts[i].dI + pts[i].dO;
    }

//      Validate();

  SetFlag (need_edges_sorting, true);
  if ( directed == fill_justDont ) {
    SortEdges();
  } else {
    GetWindings (a);
  }
//  Plot (98.0, 112.0, 8.0, 400.0, 400.0, true, true, true, true);
//      Plot(225.0,215.0,32.0,400.0,400.0,true,true,true,true);

  if (directed == fill_positive)
  {
    if (invert)
    {
      for (int i = 0; i < nbAr; i++)
	    {
	      if (swdData[i].leW < 0 && swdData[i].riW >= 0)
        {
          eData[i].weight = 1;
        }
	      else if (swdData[i].leW >= 0 && swdData[i].riW < 0)
        {
          Inverse (i);
          eData[i].weight = 1;
        }
	      else
        {
          eData[i].weight = 0;
          SubEdge (i);
          i--;
        }
	    }
    }
    else
    {
      for (int i = 0; i < nbAr; i++)
	    {
	      if (swdData[i].leW > 0 && swdData[i].riW <= 0)
        {
          eData[i].weight = 1;
        }
	      else if (swdData[i].leW <= 0 && swdData[i].riW > 0)
        {
          Inverse (i);
          eData[i].weight = 1;
        }
	      else
        {
          eData[i].weight = 0;
          SubEdge (i);
          i--;
        }
	    }
    }
  }
  else if (directed == fill_nonZero)
  {
    if (invert)
    {
      for (int i = 0; i < nbAr; i++)
	    {
	      if (swdData[i].leW < 0 && swdData[i].riW == 0)
        {
          eData[i].weight = 1;
        }
	      else if (swdData[i].leW > 0 && swdData[i].riW == 0)
        {
          eData[i].weight = 1;
        }
	      else if (swdData[i].leW == 0 && swdData[i].riW < 0)
        {
          Inverse (i);
          eData[i].weight = 1;
        }
	      else if (swdData[i].leW == 0 && swdData[i].riW > 0)
        {
          Inverse (i);
          eData[i].weight = 1;
        }
	      else
        {
          eData[i].weight = 0;
          SubEdge (i);
          i--;
        }
	    }
    }
    else
    {
      for (int i = 0; i < nbAr; i++)
	    {
	      if (swdData[i].leW > 0 && swdData[i].riW == 0)
        {
          eData[i].weight = 1;
        }
	      else if (swdData[i].leW < 0 && swdData[i].riW == 0)
        {
          eData[i].weight = 1;
        }
	      else if (swdData[i].leW == 0 && swdData[i].riW > 0)
        {
          Inverse (i);
          eData[i].weight = 1;
        }
	      else if (swdData[i].leW == 0 && swdData[i].riW < 0)
        {
          Inverse (i);
          eData[i].weight = 1;
        }
	      else
        {
          eData[i].weight = 0;
          SubEdge (i);
          i--;
        }
	    }
    }
  }
  else if (directed == fill_oddEven)
  {
    for (int i = 0; i < nbAr; i++)
    {
      swdData[i].leW %= 2;
      swdData[i].riW %= 2;
      if (swdData[i].leW < 0)
        swdData[i].leW = -swdData[i].leW;
      if (swdData[i].riW < 0)
        swdData[i].riW = -swdData[i].riW;
      if (swdData[i].leW > 0 && swdData[i].riW <= 0)
	    {
	      eData[i].weight = 1;
	    }
      else if (swdData[i].leW <= 0 && swdData[i].riW > 0)
	    {
	      Inverse (i);
	      eData[i].weight = 1;
	    }
      else
	    {
	      eData[i].weight = 0;
	      SubEdge (i);
	      i--;
	    }
    }
  } else if ( directed == fill_justDont ) {
    for (int i=0;i<nbAr;i++) {
      if ( aretes[i].st < 0 || aretes[i].en < 0 ) {
        SubEdge(i);
        i--;
      } else {
	      eData[i].weight = 0;
      }
    }
  }
  
//      Plot(200.0,200.0,2.0,400.0,400.0,true,true,true,true);

  if (GetFlag (has_sweep_data))
    {
      SweepTree::DestroyList (sTree);
      SweepEvent::DestroyQueue (sEvts);
      SetFlag (has_sweep_data, false);
    }
  MakePointData (false);
  MakeEdgeData (false);
  MakeSweepSrcData (false);
  MakeSweepDestData (false);
  a->CleanupSweep ();
  if ( directed == fill_justDont ) {
  } else {
    if (Eulerian (true) == false)
    {
      //              printf( "pas euclidian2");
      nbPt = nbAr = 0;
      return shape_euler_err;
    }
  }
  type = shape_polygon;
  return 0;
}

// technically it's just a ConvertToShape() on 2 polygons at the same time, and different rules
// for choosing the edges according to their winding numbers.
// probably one of the biggest function i ever wrote.
int
Shape::Booleen (Shape * a, Shape * b, BooleanOp mod,int cutPathID)
{
  if (a == b || a == NULL || b == NULL)
    return shape_input_err;
  Reset (0, 0);
  if (a->nbPt <= 1 || a->nbAr <= 1)
    return 0;
  if (b->nbPt <= 1 || b->nbAr <= 1)
    return 0;
  if ( mod == bool_op_cut ) {
  } else if ( mod == bool_op_slice ) {
  } else {
    if (a->type != shape_polygon)
      return shape_input_err;
    if (b->type != shape_polygon)
      return shape_input_err;
  }
  
  a->ResetSweep ();
  b->ResetSweep ();

  if (GetFlag (has_sweep_data))
    {
    }
  else
    {
      SweepTree::CreateList (sTree, a->nbAr + b->nbAr);
      SweepEvent::CreateQueue (sEvts, a->nbAr + b->nbAr);
      SetFlag (has_sweep_data, true);
    }
  MakePointData (true);
  MakeEdgeData (true);
  MakeSweepSrcData (true);
  MakeSweepDestData (true);
  if (a->HasBackData () && b->HasBackData ())
    {
      MakeBackData (true);
    }
  else
    {
      MakeBackData (false);
    }

  for (int i = 0; i < a->nbPt; i++)
    {
      a->pData[i].pending = 0;
      a->pData[i].edgeOnLeft = -1;
      a->pData[i].nextLinkedPoint = -1;
      a->pData[i].rx[0] = Round (a->pts[i].x[0]);
      a->pData[i].rx[1] = Round (a->pts[i].x[1]);
    }
  for (int i = 0; i < b->nbPt; i++)
    {
      b->pData[i].pending = 0;
      b->pData[i].edgeOnLeft = -1;
      b->pData[i].nextLinkedPoint = -1;
      b->pData[i].rx[0] = Round (b->pts[i].x[0]);
      b->pData[i].rx[1] = Round (b->pts[i].x[1]);
    }
  for (int i = 0; i < a->nbAr; i++)
    {
      a->eData[i].rdx =
	a->pData[a->aretes[i].en].rx - a->pData[a->aretes[i].st].rx;
      a->eData[i].length = 	dot(a->eData[i].rdx,a->eData[i].rdx);
      a->eData[i].ilength = 1 / a->eData[i].length;
      a->eData[i].sqlength = sqrt (a->eData[i].length);
      a->eData[i].isqlength = 1 / a->eData[i].sqlength;
      a->eData[i].siEd = a->eData[i].rdx[1] * a->eData[i].isqlength;
      a->eData[i].coEd = a->eData[i].rdx[0] * a->eData[i].isqlength;
      if (a->eData[i].siEd < 0)
	{
	  a->eData[i].siEd = -a->eData[i].siEd;
	  a->eData[i].coEd = -a->eData[i].coEd;
	}

      a->swsData[i].misc = NULL;
      a->swsData[i].firstLinkedPoint = -1;
      a->swsData[i].stPt = a->swsData[i].enPt = -1;
      a->swsData[i].leftRnd = a->swsData[i].rightRnd = -1;
      a->swsData[i].nextSh = NULL;
      a->swsData[i].nextBo = -1;
      a->swsData[i].curPoint = -1;
      a->swsData[i].doneTo = -1;
    }
  for (int i = 0; i < b->nbAr; i++)
    {
      b->eData[i].rdx =
	b->pData[b->aretes[i].en].rx - b->pData[b->aretes[i].st].rx;
      b->eData[i].length = dot(b->eData[i].rdx , b->eData[i].rdx );
      b->eData[i].ilength = 1 / b->eData[i].length;
      b->eData[i].sqlength = sqrt (b->eData[i].length);
      b->eData[i].isqlength = 1 / b->eData[i].sqlength;

      b->eData[i].siEd = b->eData[i].rdx[1] * b->eData[i].isqlength;
      b->eData[i].coEd = b->eData[i].rdx[0] * b->eData[i].isqlength;
      if (b->eData[i].siEd < 0)
      {
        b->eData[i].siEd = -b->eData[i].siEd;
        b->eData[i].coEd = -b->eData[i].coEd;
      }
      b->swsData[i].misc = NULL;
      b->swsData[i].firstLinkedPoint = -1;
      b->swsData[i].stPt = b->swsData[i].enPt = -1;
      b->swsData[i].leftRnd = b->swsData[i].rightRnd = -1;
      b->swsData[i].nextSh = NULL;
      b->swsData[i].nextBo = -1;
      b->swsData[i].curPoint = -1;
      b->swsData[i].doneTo = -1;
    }

  a->SortPointsRounded ();
  b->SortPointsRounded ();

  chgts = NULL;
  nbChgt = maxChgt = 0;

  double lastChange =
    (a->pData[0].rx[1] <
     b->pData[0].rx[1]) ? a->pData[0].rx[1] - 1.0 : b->pData[0].rx[1] - 1.0;
  int lastChgtPt = 0;
  int edgeHead = -1;
  Shape *shapeHead = NULL;

  iData = NULL;
  nbInc = maxInc = 0;

  int curAPt = 0;
  int curBPt = 0;

  while (curAPt < a->nbPt || curBPt < b->nbPt || sEvts.nbEvt > 0)
    {
/*		for (int i=0;i<sEvts.nbEvt;i++) {
			printf("%f %f %i %i\n",sEvts.events[i].posx,sEvts.events[i].posy,sEvts.events[i].leftSweep->bord,sEvts.events[i].rightSweep->bord);
		}
		//		cout << endl;
		if ( sTree.racine ) {
			SweepTree*  ct=static_cast <SweepTree*> (sTree.racine->Leftmost());
			while ( ct ) {
				printf("%i %i [%i\n",ct->bord,ct->startPoint,(ct->src==a)?1:0);
				ct=static_cast <SweepTree*> (ct->rightElem);
			}
		}
		printf("\n");*/

    NR::Point ptX;
      double ptL, ptR;
      SweepTree *intersL = NULL;
      SweepTree *intersR = NULL;
      int nPt = -1;
      Shape *ptSh = NULL;
      bool isIntersection = false;

      if (SweepEvent::
	  PeekInQueue (intersL, intersR, ptX, ptL, ptR, sEvts))
	{
	  if (curAPt < a->nbPt)
	    {
	      if (curBPt < b->nbPt)
		{
		  if (a->pData[curAPt].rx[1] < b->pData[curBPt].rx[1]
		      || (a->pData[curAPt].rx[1] == b->pData[curBPt].rx[1]
			  && a->pData[curAPt].rx[0] < b->pData[curBPt].rx[0]))
		    {
		      if (a->pData[curAPt].pending > 0
			  || (a->pData[curAPt].rx[1] > ptX[1]
			      || (a->pData[curAPt].rx[1] == ptX[1]
				  && a->pData[curAPt].rx[0] > ptX[0])))
			{
			  SweepEvent::ExtractFromQueue (intersL, intersR, ptX, ptL, ptR, sEvts);
			  isIntersection = true;
			}
		      else
			{
			  nPt = curAPt++;
			  ptSh = a;
			  ptX = ptSh->pData[nPt].rx;
			  isIntersection = false;
			}
		    }
		  else
		    {
		      if (b->pData[curBPt].pending > 0
			  || (b->pData[curBPt].rx[1] > ptX[1]
			      || (b->pData[curBPt].rx[1] == ptX[1]
				  && b->pData[curBPt].rx[0] > ptX[0])))
			{
			  SweepEvent::ExtractFromQueue (intersL, intersR, ptX, ptL, ptR, sEvts);
			  isIntersection = true;
			}
		      else
			{
			  nPt = curBPt++;
			  ptSh = b;
			  ptX = ptSh->pData[nPt].rx;
			  isIntersection = false;
			}
		    }
		}
	      else
		{
		  if (a->pData[curAPt].pending > 0
		      || (a->pData[curAPt].rx[1] > ptX[1]
			  || (a->pData[curAPt].rx[1] == ptX[1]
			      && a->pData[curAPt].rx[0] > ptX[0])))
		    {
		      SweepEvent::ExtractFromQueue (intersL, intersR, ptX, ptL, ptR, sEvts);
		      isIntersection = true;
		    }
		  else
		    {
		      nPt = curAPt++;
		      ptSh = a;
		      ptX = ptSh->pData[nPt].rx;
		      isIntersection = false;
		    }
		}
	    }
	  else
	    {
	      if (b->pData[curBPt].pending > 0
		  || (b->pData[curBPt].rx[1] > ptX[1]
		      || (b->pData[curBPt].rx[1] == ptX[1]
			  && b->pData[curBPt].rx[0] > ptX[0])))
		{
		  SweepEvent::ExtractFromQueue (intersL, intersR, ptX,  ptL, ptR, sEvts);
		  isIntersection = true;
		}
	      else
		{
		  nPt = curBPt++;
		  ptSh = b;
		  ptX = ptSh->pData[nPt].rx;
		  isIntersection = false;
		}
	    }
	}
      else
	{
	  if (curAPt < a->nbPt)
	    {
	      if (curBPt < b->nbPt)
		{
		  if (a->pData[curAPt].rx[1] < b->pData[curBPt].rx[1]
		      || (a->pData[curAPt].rx[1] == b->pData[curBPt].rx[1]
			  && a->pData[curAPt].rx[0] < b->pData[curBPt].rx[0]))
		    {
		      nPt = curAPt++;
		      ptSh = a;
		    }
		  else
		    {
		      nPt = curBPt++;
		      ptSh = b;
		    }
		}
	      else
		{
		  nPt = curAPt++;
		  ptSh = a;
		}
	    }
	  else
	    {
	      nPt = curBPt++;
	      ptSh = b;
	    }
	  ptX = ptSh->pData[nPt].rx;
	  isIntersection = false;
	}

      if (isIntersection == false)
	{
	  if (ptSh->pts[nPt].dI == 0 && ptSh->pts[nPt].dO == 0)
	    continue;
	}

      NR::Point rPtX;
      rPtX[0]= Round (ptX[0]);
      rPtX[1]= Round (ptX[1]);
      int lastPointNo = -1;
      lastPointNo = AddPoint (rPtX);
      pData[lastPointNo].rx = rPtX;

      if (rPtX[1] > lastChange)
	{
	  int lastI = AssemblePoints (lastChgtPt, lastPointNo);

	  for (int i = lastChgtPt; i < lastI; i++)
	    {
	      if (pData[i].askForWindingS)
		{
		  Shape *windS = pData[i].askForWindingS;
		  int windB = pData[i].askForWindingB;
		  pData[i].nextLinkedPoint =
		    windS->swsData[windB].firstLinkedPoint;
		  windS->swsData[windB].firstLinkedPoint = i;
		}
	    }

	  Shape *curSh = shapeHead;
	  int curBo = edgeHead;
	  while (curSh)
	    {
	      curSh->swsData[curBo].leftRnd =
		pData[curSh->swsData[curBo].leftRnd].newInd;
	      curSh->swsData[curBo].rightRnd =
		pData[curSh->swsData[curBo].rightRnd].newInd;

	      Shape *neSh = curSh->swsData[curBo].nextSh;
	      curBo = curSh->swsData[curBo].nextBo;
	      curSh = neSh;
	    }

	  for (int i = 0; i < nbChgt; i++)
	    {
	      chgts[i].ptNo = pData[chgts[i].ptNo].newInd;
	      if (chgts[i].type == 0)
		{
		  if (chgts[i].src->aretes[chgts[i].bord].st <
		      chgts[i].src->aretes[chgts[i].bord].en)
		    {
		      chgts[i].src->swsData[chgts[i].bord].stPt =
			chgts[i].ptNo;
		    }
		  else
		    {
		      chgts[i].src->swsData[chgts[i].bord].enPt =
			chgts[i].ptNo;
		    }
		}
	      else if (chgts[i].type == 1)
		{
		  if (chgts[i].src->aretes[chgts[i].bord].st >
		      chgts[i].src->aretes[chgts[i].bord].en)
		    {
		      chgts[i].src->swsData[chgts[i].bord].stPt =
			chgts[i].ptNo;
		    }
		  else
		    {
		      chgts[i].src->swsData[chgts[i].bord].enPt =
			chgts[i].ptNo;
		    }
		}
	    }

	  CheckAdjacencies (lastI, lastChgtPt, shapeHead, edgeHead);

	  CheckEdges (lastI, lastChgtPt, a, b, mod);

	  if (lastI < lastPointNo)
	    {
	      pts[lastI] = pts[lastPointNo];
	      pData[lastI] = pData[lastPointNo];
	    }
	  lastPointNo = lastI;
	  nbPt = lastI + 1;

	  lastChgtPt = lastPointNo;
	  lastChange = rPtX[1];
	  nbChgt = 0;
	  edgeHead = -1;
	  shapeHead = NULL;
	}


      if (isIntersection)
	{
	  // les 2 events de part et d'autre de l'intersection
	  // (celui de l'intersection a deja ete depile)
	  intersL->RemoveEvent (sEvts, true);
	  intersR->RemoveEvent (sEvts, false);

	  AddChgt (lastPointNo, lastChgtPt, shapeHead, edgeHead, 2,
		   intersL->src, intersL->bord, intersR->src, intersR->bord);

	  intersL->SwapWithRight (sTree, sEvts);

	  TesteIntersection (intersL, true, true);
	  TesteIntersection (intersR, false, true);
	}
      else
	{
	  int cb;

	  int nbUp = 0, nbDn = 0;
	  int upNo = -1, dnNo = -1;
	  cb = ptSh->pts[nPt].firstA;
	  while (cb >= 0 && cb < ptSh->nbAr)
	    {
	      if ((ptSh->aretes[cb].st < ptSh->aretes[cb].en
		   && nPt == ptSh->aretes[cb].en)
		  || (ptSh->aretes[cb].st > ptSh->aretes[cb].en
		      && nPt == ptSh->aretes[cb].st))
		{
		  upNo = cb;
		  nbUp++;
		}
	      if ((ptSh->aretes[cb].st > ptSh->aretes[cb].en
		   && nPt == ptSh->aretes[cb].en)
		  || (ptSh->aretes[cb].st < ptSh->aretes[cb].en
		      && nPt == ptSh->aretes[cb].st))
		{
		  dnNo = cb;
		  nbDn++;
		}
	      cb = ptSh->NextAt (nPt, cb);
	    }

	  if (nbDn <= 0)
	    {
	      upNo = -1;
	    }
	  if (upNo >= 0 && (SweepTree *) ptSh->swsData[upNo].misc == NULL)
	    {
	      upNo = -1;
	    }

//                      upNo=-1;

	  bool doWinding = true;

	  if (nbUp > 0)
	    {
	      cb = ptSh->pts[nPt].firstA;
	      while (cb >= 0 && cb < ptSh->nbAr)
		{
		  if ((ptSh->aretes[cb].st < ptSh->aretes[cb].en
		       && nPt == ptSh->aretes[cb].en)
		      || (ptSh->aretes[cb].st > ptSh->aretes[cb].en
			  && nPt == ptSh->aretes[cb].st))
		    {
		      if (cb != upNo)
			{
			  SweepTree *node =
			    (SweepTree *) ptSh->swsData[cb].misc;
			  if (node == NULL)
			    {
			    }
			  else
			    {
			      AddChgt (lastPointNo, lastChgtPt, shapeHead,
				       edgeHead, 1, node->src, node->bord,
				       NULL, -1);
			      ptSh->swsData[cb].misc = NULL;

			      int onLeftB = -1, onRightB = -1;
			      Shape *onLeftS = NULL;
			      Shape *onRightS = NULL;
			      if (node->leftElem)
				{
				  onLeftB =
				    (static_cast <
				     SweepTree * >(node->leftElem))->bord;
				  onLeftS =
				    (static_cast <
				     SweepTree * >(node->leftElem))->src;
				}
			      if (node->rightElem)
				{
				  onRightB =
				    (static_cast <
				     SweepTree * >(node->rightElem))->bord;
				  onRightS =
				    (static_cast <
				     SweepTree * >(node->rightElem))->src;
				}

			      node->Remove (sTree, sEvts, true);
			      if (onLeftS && onRightS)
				{
				  SweepTree *onLeft =
				    (SweepTree *) onLeftS->swsData[onLeftB].
				    misc;
//                                                                      SweepTree* onRight=(SweepTree*)onRightS->swsData[onRightB].misc;
				  if (onLeftS == ptSh
				      && (onLeftS->aretes[onLeftB].en == nPt
					  || onLeftS->aretes[onLeftB].st ==
					  nPt))
				    {
				    }
				  else
				    {
				      if (onRightS == ptSh
					  && (onRightS->aretes[onRightB].en ==
					      nPt
					      || onRightS->aretes[onRightB].
					      st == nPt))
					{
					}
				      else
					{
					  TesteIntersection (onLeft, false,
							     true);
					}
				    }
				}
			    }
			}
		    }
		  cb = ptSh->NextAt (nPt, cb);
		}
	    }

	  // traitement du "upNo devient dnNo"
	  SweepTree *insertionNode = NULL;
	  if (dnNo >= 0)
	    {
	      if (upNo >= 0)
		{
		  SweepTree *node = (SweepTree *) ptSh->swsData[upNo].misc;

		  AddChgt (lastPointNo, lastChgtPt, shapeHead, edgeHead, 1,
			   node->src, node->bord, NULL, -1);

		  ptSh->swsData[upNo].misc = NULL;

		  node->RemoveEvents (sEvts);
		  node->ConvertTo (ptSh, dnNo, 1, lastPointNo);
		  ptSh->swsData[dnNo].misc = node;
		  TesteIntersection (node, false, true);
		  TesteIntersection (node, true, true);
		  insertionNode = node;

		  ptSh->swsData[dnNo].curPoint = lastPointNo;

		  AddChgt (lastPointNo, lastChgtPt, shapeHead, edgeHead, 0,
			   node->src, node->bord, NULL, -1);
		}
	      else
		{
		  SweepTree *node =
		    SweepTree::AddInList (ptSh, dnNo, 1, lastPointNo, sTree,
					  this);
		  ptSh->swsData[dnNo].misc = node;
		  node->Insert (sTree, sEvts, this, lastPointNo, true);

		  if (doWinding)
		    {
		      SweepTree *myLeft =
			static_cast < SweepTree * >(node->leftElem);
		      if (myLeft)
			{
			  pData[lastPointNo].askForWindingS = myLeft->src;
			  pData[lastPointNo].askForWindingB = myLeft->bord;
			}
		      else
			{
			  pData[lastPointNo].askForWindingB = -1;
			}
		      doWinding = false;
		    }

		  TesteIntersection (node, false, true);
		  TesteIntersection (node, true, true);
		  insertionNode = node;

		  ptSh->swsData[dnNo].curPoint = lastPointNo;

		  AddChgt (lastPointNo, lastChgtPt, shapeHead, edgeHead, 0,
			   node->src, node->bord, NULL, -1);
		}
	    }

	  if (nbDn > 1)
	    {			// si nbDn == 1 , alors dnNo a deja ete traite
	      cb = ptSh->pts[nPt].firstA;
	      while (cb >= 0 && cb < ptSh->nbAr)
		{
		  if ((ptSh->aretes[cb].st > ptSh->aretes[cb].en
		       && nPt == ptSh->aretes[cb].en)
		      || (ptSh->aretes[cb].st < ptSh->aretes[cb].en
			  && nPt == ptSh->aretes[cb].st))
		    {
		      if (cb != dnNo)
			{
			  SweepTree *node =
			    SweepTree::AddInList (ptSh, cb, 1, lastPointNo,
						  sTree, this);
			  ptSh->swsData[cb].misc = node;
//                                                      node->Insert(sTree,sEvts,this,lastPointNo,true);
			  node->InsertAt (sTree, sEvts, this, insertionNode,
					  nPt, true);

			  if (doWinding)
			    {
			      SweepTree *myLeft =
				static_cast < SweepTree * >(node->leftElem);
			      if (myLeft)
				{
				  pData[lastPointNo].askForWindingS =
				    myLeft->src;
				  pData[lastPointNo].askForWindingB =
				    myLeft->bord;
				}
			      else
				{
				  pData[lastPointNo].askForWindingB = -1;
				}
			      doWinding = false;
			    }

			  TesteIntersection (node, false, true);
			  TesteIntersection (node, true, true);

			  ptSh->swsData[cb].curPoint = lastPointNo;

			  AddChgt (lastPointNo, lastChgtPt, shapeHead,
				   edgeHead, 0, node->src, node->bord, NULL,
				   -1);
			}
		    }
		  cb = ptSh->NextAt (nPt, cb);
		}
	    }
	}
    }
  {
    int lastI = AssemblePoints (lastChgtPt, nbPt);

    for (int i = lastChgtPt; i < lastI; i++)
      {
	if (pData[i].askForWindingS)
	  {
	    Shape *windS = pData[i].askForWindingS;
	    int windB = pData[i].askForWindingB;
	    pData[i].nextLinkedPoint = windS->swsData[windB].firstLinkedPoint;
	    windS->swsData[windB].firstLinkedPoint = i;
	  }
      }

    Shape *curSh = shapeHead;
    int curBo = edgeHead;
    while (curSh)
      {
	curSh->swsData[curBo].leftRnd =
	  pData[curSh->swsData[curBo].leftRnd].newInd;
	curSh->swsData[curBo].rightRnd =
	  pData[curSh->swsData[curBo].rightRnd].newInd;

	Shape *neSh = curSh->swsData[curBo].nextSh;
	curBo = curSh->swsData[curBo].nextBo;
	curSh = neSh;
      }

    for (int i = 0; i < nbChgt; i++)
      {
	chgts[i].ptNo = pData[chgts[i].ptNo].newInd;
	if (chgts[i].type == 0)
	  {
	    if (chgts[i].src->aretes[chgts[i].bord].st <
		chgts[i].src->aretes[chgts[i].bord].en)
	      {
		chgts[i].src->swsData[chgts[i].bord].stPt = chgts[i].ptNo;
	      }
	    else
	      {
		chgts[i].src->swsData[chgts[i].bord].enPt = chgts[i].ptNo;
	      }
	  }
	else if (chgts[i].type == 1)
	  {
	    if (chgts[i].src->aretes[chgts[i].bord].st >
		chgts[i].src->aretes[chgts[i].bord].en)
	      {
		chgts[i].src->swsData[chgts[i].bord].stPt = chgts[i].ptNo;
	      }
	    else
	      {
		chgts[i].src->swsData[chgts[i].bord].enPt = chgts[i].ptNo;
	      }
	  }
      }

    CheckAdjacencies (lastI, lastChgtPt, shapeHead, edgeHead);

    CheckEdges (lastI, lastChgtPt, a, b, mod);

    nbPt = lastI;

    edgeHead = -1;
    shapeHead = NULL;
  }

  if (chgts)
    free (chgts);
  chgts = NULL;
  nbChgt = maxChgt = 0;


//      MakeAretes(a,true);
/*	if ( mod == bool_op_diff || mod == bool_op_symdiff ) {
		MakeAretes(b,false);
	} else {
		MakeAretes(b,true);
	}*/

  if (iData)
    free (iData);
  iData = NULL;
  nbInc = maxInc = 0;

//      Plot(190,70,6,400,400,true,false,true,true);

  if ( mod == bool_op_cut ) {
    AssembleAretes (fill_justDont);
    // dupliquer les aretes de la coupure
    int i=nbAr-1;
    for (;i>=0;i--) {
      if ( ebData[i].pathID == cutPathID ) {
        // on duplique
        int nEd=AddEdge(aretes[i].en,aretes[i].st);
        ebData[nEd].pathID=cutPathID;
        ebData[nEd].pieceID=ebData[i].pieceID;
        ebData[nEd].tSt=ebData[i].tEn;
        ebData[nEd].tEn=ebData[i].tSt;
        eData[nEd].weight=eData[i].weight;
        // lui donner les firstlinkedpoitn si besoin
        if ( aretes[i].en >= aretes[i].st ) {
          int cp = swsData[i].firstLinkedPoint;
          while (cp >= 0) {
            pData[cp].askForWindingB = nEd;
            cp = pData[cp].nextLinkedPoint;
          }
          swsData[nEd].firstLinkedPoint = swsData[i].firstLinkedPoint;
          swsData[i].firstLinkedPoint=-1;
        }
      }
    }
  } else if ( mod == bool_op_slice ) {
  } else {
    AssembleAretes ();
  }
  
  for (int i = 0; i < nbPt; i++)
    {
      pts[i].oldDegree = pts[i].dI + pts[i].dO;
    }

  SetFlag (need_edges_sorting, true);
  if ( mod == bool_op_slice ) {
  } else {
    GetWindings (a, b, mod, false);
  }
//      Plot(190,70,6,400,400,true,true,true,true);

  if (mod == bool_op_symdiff)
  {
    for (int i = 0; i < nbAr; i++)
    {
      swdData[i].leW = swdData[i].leW % 2;
      if (swdData[i].leW < 0)
        swdData[i].leW = -swdData[i].leW;
      swdData[i].riW = swdData[i].riW;
      if (swdData[i].riW < 0)
        swdData[i].riW = -swdData[i].riW;
      
      if (swdData[i].leW > 0 && swdData[i].riW <= 0)
	    {
	      eData[i].weight = 1;
	    }
      else if (swdData[i].leW <= 0 && swdData[i].riW > 0)
	    {
	      Inverse (i);
	      eData[i].weight = 1;
	    }
      else
	    {
	      eData[i].weight = 0;
	      SubEdge (i);
	      i--;
	    }
    }
  }
  else if (mod == bool_op_union || mod == bool_op_diff)
  {
    for (int i = 0; i < nbAr; i++)
    {
      if (swdData[i].leW > 0 && swdData[i].riW <= 0)
	    {
	      eData[i].weight = 1;
	    }
      else if (swdData[i].leW <= 0 && swdData[i].riW > 0)
	    {
	      Inverse (i);
	      eData[i].weight = 1;
	    }
      else
	    {
	      eData[i].weight = 0;
	      SubEdge (i);
	      i--;
	    }
    }
  }
  else if (mod == bool_op_inters)
  {
    for (int i = 0; i < nbAr; i++)
    {
      if (swdData[i].leW > 1 && swdData[i].riW <= 1)
	    {
	      eData[i].weight = 1;
	    }
      else if (swdData[i].leW <= 1 && swdData[i].riW > 1)
	    {
	      Inverse (i);
	      eData[i].weight = 1;
	    }
      else
	    {
	      eData[i].weight = 0;
	      SubEdge (i);
	      i--;
	    }
    }
  } else if ( mod == bool_op_cut ) {
    // inverser les aretes de la coupe au besoin
    for (int i=0;i<nbAr;i++) {
      if ( aretes[i].st < 0 || aretes[i].en < 0 ) {
        if ( i < nbAr-1 ) {
          // decaler les askForWinding
          int cp = swsData[nbAr-1].firstLinkedPoint;
          while (cp >= 0) {
            pData[cp].askForWindingB = i;
            cp = pData[cp].nextLinkedPoint;
          }
        }
        SwapEdges(i,nbAr-1);
        SubEdge(nbAr-1);
//        SubEdge(i);
        i--;
      } else if ( ebData[i].pathID == cutPathID ) {
        swdData[i].leW=swdData[i].leW%2;
        swdData[i].riW=swdData[i].riW%2;
        if ( swdData[i].leW < swdData[i].riW ) {
          Inverse(i);
        }
      }
    }
  } else if ( mod == bool_op_slice ) {
    // supprimer les aretes de la coupe
    int i=nbAr-1;
    for (;i>=0;i--) {
      if ( ebData[i].pathID == cutPathID || aretes[i].st < 0 || aretes[i].en < 0 ) {
        SubEdge(i);
      }
    }
  }
  else
  {
    for (int i = 0; i < nbAr; i++)
    {
      if (swdData[i].leW > 0 && swdData[i].riW <= 0)
	    {
	      eData[i].weight = 1;
	    }
      else if (swdData[i].leW <= 0 && swdData[i].riW > 0)
	    {
	      Inverse (i);
	      eData[i].weight = 1;
	    }
      else
	    {
	      eData[i].weight = 0;
	      SubEdge (i);
	      i--;
	    }
    }
  }
  
  if (GetFlag (has_sweep_data))
    {
      SweepTree::DestroyList (sTree);
      SweepEvent::DestroyQueue (sEvts);
      SetFlag (has_sweep_data, false);
    }
  if ( mod == bool_op_cut ) {
    // on garde le askForWinding
  } else {
    MakePointData (false);
  }
  MakeEdgeData (false);
  MakeSweepSrcData (false);
  MakeSweepDestData (false);
  a->CleanupSweep ();
  b->CleanupSweep ();

  if (Eulerian (true) == false)
    {
//              printf( "pas euclidian2");
      nbPt = nbAr = 0;
      return shape_euler_err;
    }
  type = shape_polygon;
  return 0;
}

// frontend to the TesteIntersection() below
void
Shape::TesteIntersection (SweepTree * t, bool onLeft, bool onlyDiff)
{
  if (onLeft)
    {
      SweepTree *tL = static_cast < SweepTree * >(t->leftElem);
      if (tL)
	{
        NR::Point atx;
        double     atl, atr;
	  if (TesteIntersection (tL, t, atx, atl, atr, onlyDiff))
	    {
	      SweepEvent::AddInQueue (tL, t, atx, atl, atr, sEvts);
	    }
	}
    }
  else
    {
      SweepTree *tR = static_cast < SweepTree * >(t->rightElem);
      if (tR)
	{
        NR::Point  atx;
	  double atl, atr;
	  if (TesteIntersection (t, tR, atx, atl, atr, onlyDiff))
	    {
	      SweepEvent::AddInQueue (t, tR, atx, atl, atr, sEvts);
	    }
	}
    }
}
// a crucial piece of code: computing intersections between segments
bool
Shape::TesteIntersection (SweepTree * iL, SweepTree * iR, NR::Point &atx, double &atL, double &atR, bool onlyDiff)
{
  int lSt = iL->src->aretes[iL->bord].st, lEn = iL->src->aretes[iL->bord].en;
  int rSt = iR->src->aretes[iR->bord].st, rEn = iR->src->aretes[iR->bord].en;
  NR::Point ldir, rdir;
  ldir = iL->src->eData[iL->bord].rdx;
  rdir = iR->src->eData[iR->bord].rdx;
  // first, a round of checks to quickly dismiss edge which obviously dont intersect,
  // such as having disjoint bounding boxes
  if (lSt < lEn)
    {
    }
  else
    {
      int swap = lSt;
      lSt = lEn;
      lEn = swap;
      ldir = -ldir;
    }
  if (rSt < rEn)
    {
    }
  else
    {
      int swap = rSt;
      rSt = rEn;
      rEn = swap;
      rdir = -rdir;
    }

  if (iL->src->pData[lSt].rx[0] < iL->src->pData[lEn].rx[0])
    {
      if (iR->src->pData[rSt].rx[0] < iR->src->pData[rEn].rx[0])
	{
	  if (iL->src->pData[lSt].rx[0] > iR->src->pData[rEn].rx[0])
	    return false;
	  if (iL->src->pData[lEn].rx[0] < iR->src->pData[rSt].rx[0])
	    return false;
	}
      else
	{
	  if (iL->src->pData[lSt].rx[0] > iR->src->pData[rSt].rx[0])
	    return false;
	  if (iL->src->pData[lEn].rx[0] < iR->src->pData[rEn].rx[0])
	    return false;
	}
    }
  else
    {
      if (iR->src->pData[rSt].rx[0] < iR->src->pData[rEn].rx[0])
	{
	  if (iL->src->pData[lEn].rx[0] > iR->src->pData[rEn].rx[0])
	    return false;
	  if (iL->src->pData[lSt].rx[0] < iR->src->pData[rSt].rx[0])
	    return false;
	}
      else
	{
	  if (iL->src->pData[lEn].rx[0] > iR->src->pData[rSt].rx[0])
	    return false;
	  if (iL->src->pData[lSt].rx[0] < iR->src->pData[rEn].rx[0])
	    return false;
	}
    }

  double ang = cross (rdir, ldir);
//      ang*=iL->src->eData[iL->bord].isqlength;
//      ang*=iR->src->eData[iR->bord].isqlength;
  if (ang <= 0) return false;		// edges in opposite directions:  <-left  ... right ->
                                // they can't intersect

  // d'abord tester les bords qui partent d'un meme point
  if (iL->src == iR->src && lSt == rSt)
    {
      if (iL->src == iR->src && lEn == rEn)
	return false;		// c'est juste un doublon
      atx = iL->src->pData[lSt].rx;
      atR = atL = -1;
      return true;		// l'ordre est mauvais
    }
  if (iL->src == iR->src && lEn == rEn)
    return false;		// rien a faire=ils vont terminer au meme endroit

  // tester si on est dans une intersection multiple

  if (onlyDiff && iL->src == iR->src)
    return false;

  // on reprend les vrais points
  lSt = iL->src->aretes[iL->bord].st;
  lEn = iL->src->aretes[iL->bord].en;
  rSt = iR->src->aretes[iR->bord].st;
  rEn = iR->src->aretes[iR->bord].en;

  // compute intersection (if there is one)
  // Boissonat anr Preparata said in one paper that double precision floats were sufficient for get single precision
  // coordinates for the intersection, if the endpoints are single precision. i hope they're right...
  {
    NR::Point sDiff, eDiff;
    double slDot, elDot;
    double srDot, erDot;
    sDiff = iL->src->pData[lSt].rx - iR->src->pData[rSt].rx;
    eDiff = iL->src->pData[lEn].rx - iR->src->pData[rSt].rx;
    srDot = cross (sDiff,rdir);
    erDot = cross (eDiff,rdir);
    sDiff = iR->src->pData[rSt].rx - iL->src->pData[lSt].rx;
    eDiff = iR->src->pData[rEn].rx - iL->src->pData[lSt].rx;
    slDot = cross (sDiff,ldir);
    elDot = cross (eDiff,ldir);

    if ((srDot >= 0 && erDot >= 0) || (srDot <= 0 && erDot <= 0))
      {
	if (srDot == 0)
	  {
	    if (lSt < lEn)
	      {
		atx = iL->src->pData[lSt].rx;
		atL = 0;
		atR = slDot / (slDot - elDot);
		return true;
	      }
	    else
	      {
		return false;
	      }
	  }
	else if (erDot == 0)
	  {
	    if (lSt > lEn)
	      {
		atx = iL->src->pData[lEn].rx;
		atL = 1;
		atR = slDot / (slDot - elDot);
		return true;
	      }
	    else
	      {
		return false;
	      }
	  }
	if (srDot > 0 && erDot > 0)
	  {
	    if (rEn < rSt)
	      {
		if (srDot < erDot)
		  {
		    if (lSt < lEn)
		      {
			atx = iL->src->pData[lSt].rx;
			atL = 0;
			atR = slDot / (slDot - elDot);
			return true;
		      }
		  }
		else
		  {
		    if (lEn < lSt)
		      {
			atx = iL->src->pData[lEn].rx;
			atL = 1;
			atR = slDot / (slDot - elDot);
			return true;
		      }
		  }
	      }
	  }
	if (srDot < 0 && erDot < 0)
	  {
	    if (rEn > rSt)
	      {
		if (srDot > erDot)
		  {
		    if (lSt < lEn)
		      {
			atx = iL->src->pData[lSt].rx;
			atL = 0;
			atR = slDot / (slDot - elDot);
			return true;
		      }
		  }
		else
		  {
		    if (lEn < lSt)
		      {
			atx = iL->src->pData[lEn].rx;
			atL = 1;
			atR = slDot / (slDot - elDot);
			return true;
		      }
		  }
	      }
	  }
	return false;
      }
    if ((slDot >= 0 && elDot >= 0) || (slDot <= 0 && elDot <= 0))
      {
	if (slDot == 0)
	  {
	    if (rSt < rEn)
	      {
		atx = iR->src->pData[rSt].rx;
		atR = 0;
		atL = srDot / (srDot - erDot);
		return true;
	      }
	    else
	      {
		return false;
	      }
	  }
	else if (elDot == 0)
	  {
	    if (rSt > rEn)
	      {
		atx = iR->src->pData[rEn].rx;
		atR = 1;
		atL = srDot / (srDot - erDot);
		return true;
	      }
	    else
	      {
		return false;
	      }
	  }
	if (slDot > 0 && elDot > 0)
	  {
	    if (lEn > lSt)
	      {
		if (slDot < elDot)
		  {
		    if (rSt < rEn)
		      {
			atx = iR->src->pData[rSt].rx;
			atR = 0;
			atL = srDot / (srDot - erDot);
			return true;
		      }
		  }
		else
		  {
		    if (rEn < rSt)
		      {
			atx = iR->src->pData[rEn].rx;
			atR = 1;
			atL = srDot / (srDot - erDot);
			return true;
		      }
		  }
	      }
	  }
	if (slDot < 0 && elDot < 0)
	  {
	    if (lEn < lSt)
	      {
		if (slDot > elDot)
		  {
		    if (rSt < rEn)
		      {
			atx = iR->src->pData[rSt].rx;
			atR = 0;
			atL = srDot / (srDot - erDot);
			return true;
		      }
		  }
		else
		  {
		    if (rEn < rSt)
		      {
			atx = iR->src->pData[rEn].rx;
			atR = 1;
			atL = srDot / (srDot - erDot);
			return true;
		      }
		  }
	      }
	  }
	return false;
      }

/*		double  slb=slDot-elDot,srb=srDot-erDot;
		if ( slb < 0 ) slb=-slb;
		if ( srb < 0 ) srb=-srb;*/
    if (iL->src->eData[iL->bord].siEd > iR->src->eData[iR->bord].siEd)
      {
	atx =
	  (slDot * iR->src->pData[rEn].rx -
	   elDot * iR->src->pData[rSt].rx) / (slDot - elDot);
      }
    else
      {
	atx =
	  (srDot * iL->src->pData[lEn].rx -
	   erDot * iL->src->pData[lSt].rx) / (srDot - erDot);
      }
    atL = srDot / (srDot - erDot);
    atR = slDot / (slDot - elDot);
    return true;
  }

  return true;
}

int
Shape::PushIncidence (Shape * a, int cb, int pt, double theta)
{
  if (theta < 0 || theta > 1)
    return -1;

  if (nbInc >= maxInc)
    {
      maxInc = 2 * nbInc + 1;
      iData =
	(incidenceData *) realloc (iData, maxInc * sizeof (incidenceData));
    }
  int n = nbInc++;
  iData[n].nextInc = a->swsData[cb].firstLinkedPoint;
  iData[n].pt = pt;
  iData[n].theta = theta;
  a->swsData[cb].firstLinkedPoint = n;
  return n;
}

int
Shape::CreateIncidence (Shape * a, int no, int nPt)
{
  NR::Point adir, diff;
  adir = a->eData[no].rdx;
  diff = pts[nPt].x - a->pData[a->aretes[no].st].rx;
  double t = dot (diff, adir);
  t *= a->eData[no].ilength;
  return PushIncidence (a, no, nPt, t);
}

int
Shape::Winding (int nPt) const 
{
  int askTo = pData[nPt].askForWindingB;
  if (askTo < 0)
    return 0;
  if (aretes[askTo].st < aretes[askTo].en)
    {
      return swdData[askTo].leW;
    }
  else
    {
      return swdData[askTo].riW;
    }
  return 0;
}

int
Shape::Winding (const NR::Point px) const 
{
  int lr = 0, ll = 0, rr = 0;

  for (int i = 0; i < nbAr; i++)
    {
      NR::Point adir, diff, ast, aen;
      adir = eData[i].rdx;

      ast = pData[aretes[i].st].rx;
      aen = pData[aretes[i].en].rx;

      int nWeight = eData[i].weight;

      if (ast[0] < aen[0])
	{
	  if (ast[0] > px[0])
	    continue;
	  if (aen[0] < px[0])
	    continue;
	}
      else
	{
	  if (ast[0] < px[0])
	    continue;
	  if (aen[0] > px[0])
	    continue;
	}
      if (ast[0] == px[0])
	{
	  if (ast[1] >= px[1])
	    continue;
	  if (aen[0] == px[0])
	    continue;
	  if (aen[0] < px[0])
	    ll += nWeight;
	  else
	    rr -= nWeight;
	  continue;
	}
      if (aen[0] == px[0])
	{
	  if (aen[1] >= px[1])
	    continue;
	  if (ast[0] == px[0])
	    continue;
	  if (ast[0] < px[0])
	    ll -= nWeight;
	  else
	    rr += nWeight;
	  continue;
	}

      if (ast[1] < aen[1])
	{
	  if (ast[1] >= px[1])
	    continue;
	}
      else
	{
	  if (aen[1] >= px[1])
	    continue;
	}

      diff = px - ast;
      double cote = cross (diff,adir);
      if (cote == 0)
	continue;
      if (cote < 0)
	{
	  if (ast[0] > px[0])
	    lr += nWeight;
	}
      else
	{
	  if (ast[0] < px[0])
	    lr -= nWeight;
	}
    }
  return lr + (ll + rr) / 2;
}

// merging duplicate points and edges
int
Shape::AssemblePoints (int st, int en)
{
  if (en > st)
    {
      for (int i = st; i < en; i++)
	pData[i].oldInd = i;
//              SortPoints(st,en-1);
      SortPointsByOldInd (st, en - 1); // SortPointsByOldInd() is required here, because of the edges we have
                                       // associated with the point for later computation of winding numbers.
                                       // specifically, we need the first point we treated, it's the only one with a valid
                                       // associated edge (man, that was a nice bug).
      for (int i = st; i < en; i++)
	pData[pData[i].oldInd].newInd = i;

      int lastI = st;
      for (int i = st; i < en; i++)
	{
	  pData[i].pending = lastI++;
	  if (i > st && pts[i - 1].x[0] == pts[i].x[0] && pts[i - 1].x[1] == pts[i].x[1])
	    {
	      pData[i].pending = pData[i - 1].pending;
	      if (pData[pData[i].pending].askForWindingS == NULL)
		{
		  pData[pData[i].pending].askForWindingS =
		    pData[i].askForWindingS;
		  pData[pData[i].pending].askForWindingB =
		    pData[i].askForWindingB;
		}
	      else
		{
		  if (pData[pData[i].pending].askForWindingS ==
		      pData[i].askForWindingS
		      && pData[pData[i].pending].askForWindingB ==
		      pData[i].askForWindingB)
		    {
		      // meme bord, c bon
		    }
		  else
		    {
		      // meme point, mais pas le meme bord: ouille!
		      // il faut prendre le bord le plus a gauche
		      // en pratique, n'arrive que si 2 maxima sont dans la meme case -> le mauvais choix prend une arete incidente 
		      // au bon choix
//                                              printf("doh");
		    }
		}
	      lastI--;
	    }
	  else
	    {
	      if (i > pData[i].pending)
		{
		  pts[pData[i].pending].x = pts[i].x;
		  pData[pData[i].pending].rx = pts[i].x;
		  pData[pData[i].pending].askForWindingS =
		    pData[i].askForWindingS;
		  pData[pData[i].pending].askForWindingB =
		    pData[i].askForWindingB;
		}
	    }
	}
      for (int i = st; i < en; i++)
	pData[i].newInd = pData[pData[i].newInd].pending;
      return lastI;
    }
  return en;
}

void
Shape::AssemblePoints (Shape * a)
{
  if (nbPt > 0)
    {
      int lastI = AssemblePoints (0, nbPt);

      for (int i = 0; i < a->nbAr; i++)
	{
	  a->swsData[i].stPt = pData[a->swsData[i].stPt].newInd;
	  a->swsData[i].enPt = pData[a->swsData[i].enPt].newInd;
	}
      for (int i = 0; i < nbInc; i++)
	iData[i].pt = pData[iData[i].pt].newInd;

      nbPt = lastI;
    }
}
void
Shape::AssembleAretes (FillRule directed)
{
  if ( directed == fill_justDont && HasBackData() == false ) directed=fill_nonZero;
  
  for (int i = 0; i < nbPt; i++) {
    if (pts[i].dI + pts[i].dO == 2) {
      int cb, cc;
      cb = pts[i].firstA;
      cc = pts[i].lastA;
      bool  doublon=false;
      if ((aretes[cb].st == aretes[cc].st && aretes[cb].en == aretes[cc].en)
          || (aretes[cb].st == aretes[cc].en && aretes[cb].en == aretes[cc].en)) doublon=true;
      if ( directed == fill_justDont ) {
        if ( doublon ) {
          if ( ebData[cb].pathID > ebData[cc].pathID ) {
            cc = pts[i].firstA; // on swappe pour enlever cc
            cb = pts[i].lastA;
          } else if ( ebData[cb].pathID == ebData[cc].pathID ) {
            if ( ebData[cb].pieceID > ebData[cc].pieceID ) {
              cc = pts[i].firstA; // on swappe pour enlever cc
              cb = pts[i].lastA;
            } else if ( ebData[cb].pieceID == ebData[cc].pieceID ) { 
              if ( ebData[cb].tSt > ebData[cc].tSt ) {
                cc = pts[i].firstA; // on swappe pour enlever cc
                cb = pts[i].lastA;
              }
            }
          }
        }
        if ( doublon ) eData[cc].weight = 0;
      } else {
      }
      if ( doublon ) {
        if (aretes[cb].st == aretes[cc].st) {
          eData[cb].weight += eData[cc].weight;
        } else {
          eData[cb].weight -= eData[cc].weight;
        }
 	      eData[cc].weight = 0;
        
	      if (swsData[cc].firstLinkedPoint >= 0) {
          int cp = swsData[cc].firstLinkedPoint;
          while (cp >= 0) {
            pData[cp].askForWindingB = cb;
            cp = pData[cp].nextLinkedPoint;
          }
          if (swsData[cb].firstLinkedPoint < 0) {
            swsData[cb].firstLinkedPoint = swsData[cc].firstLinkedPoint;
          } else {
            int ncp = swsData[cb].firstLinkedPoint;
            while (pData[ncp].nextLinkedPoint >= 0) {
              ncp = pData[ncp].nextLinkedPoint;
            }
            pData[ncp].nextLinkedPoint = swsData[cc].firstLinkedPoint;
          }
        }
        
	      DisconnectStart (cc);
	      DisconnectEnd (cc);
	      if (nbAr > 1) {
          int cp = swsData[nbAr - 1].firstLinkedPoint;
          while (cp >= 0) {
            pData[cp].askForWindingB = cc;
            cp = pData[cp].nextLinkedPoint;
          }
        }
	      SwapEdges (cc, nbAr - 1);
	      if (cb == nbAr - 1) {
          cb = cc;
        }
	      nbAr--;
	    }
    } else {
      int cb;
      cb = pts[i].firstA;
      while (cb >= 0 && cb < nbAr) {
	      int other = Other (i, cb);
	      int cc;
	      cc = pts[i].firstA;
	      while (cc >= 0 && cc < nbAr) {
          int ncc = NextAt (i, cc);
          bool  doublon=false;
          if (cc != cb && Other (i, cc) == other ) doublon=true;
          if ( directed == fill_justDont ) {
            if ( doublon ) {
              if ( ebData[cb].pathID > ebData[cc].pathID ) {
                doublon=false;
              } else if ( ebData[cb].pathID == ebData[cc].pathID ) {
                if ( ebData[cb].pieceID > ebData[cc].pieceID ) {
                  doublon=false;
                } else if ( ebData[cb].pieceID == ebData[cc].pieceID ) { 
                  if ( ebData[cb].tSt > ebData[cc].tSt ) {
                    doublon=false;
                  }
                }
              }
            }
            if ( doublon ) eData[cc].weight = 0;
          } else {
          }
          if ( doublon ) {
//            if (cc != cb && Other (i, cc) == other) {
            // doublon
            if (aretes[cb].st == aretes[cc].st) {
              eData[cb].weight += eData[cc].weight;
            } else {
              eData[cb].weight -= eData[cc].weight;
            }
            eData[cc].weight = 0;
            
            if (swsData[cc].firstLinkedPoint >= 0) {
              int cp = swsData[cc].firstLinkedPoint;
              while (cp >= 0) {
                pData[cp].askForWindingB = cb;
                cp = pData[cp].nextLinkedPoint;
              }
              if (swsData[cb].firstLinkedPoint < 0) {
                swsData[cb].firstLinkedPoint = swsData[cc].firstLinkedPoint;
              } else {
                int ncp = swsData[cb].firstLinkedPoint;
                while (pData[ncp].nextLinkedPoint >= 0) {
                  ncp = pData[ncp].nextLinkedPoint;
                }
                pData[ncp].nextLinkedPoint = swsData[cc].firstLinkedPoint;
              }
            }
            
            DisconnectStart (cc);
            DisconnectEnd (cc);
            if (nbAr > 1) {
              int cp = swsData[nbAr - 1].firstLinkedPoint;
              while (cp >= 0) {
                pData[cp].askForWindingB = cc;
                cp = pData[cp].nextLinkedPoint;
              }
            }
            SwapEdges (cc, nbAr - 1);
            if (cb == nbAr - 1) {
              cb = cc;
            }
            if (ncc == nbAr - 1) {
              ncc = cc;
            }
            nbAr--;
          }
          cc = ncc;
        }
	      cb = NextAt (i, cb);
	    }
    }
  }
  
  if ( directed == fill_justDont ) {
    for (int i = 0; i < nbAr; i++)  {
      if (eData[i].weight == 0) {
//        SubEdge(i);
 //       i--;
      } else {
        if (eData[i].weight < 0) Inverse (i);
      }
    }
  } else {
    for (int i = 0; i < nbAr; i++)  {
      if (eData[i].weight == 0) {
        //                      SubEdge(i);
        //                      i--;
      } else {
        if (eData[i].weight < 0) Inverse (i);
      }
    }
  }
}
void
Shape::GetWindings (Shape * a, Shape * b, BooleanOp mod, bool brutal)
{
  // preparation du parcours
  for (int i = 0; i < nbAr; i++)
    {
      swdData[i].misc = 0;
      swdData[i].precParc = swdData[i].suivParc = -1;
    }

  // chainage
  SortEdges ();

  int searchInd = 0;

  int lastPtUsed = 0;
  do
    {
      int startBord = -1;
      int outsideW = 0;
      {
	int fi = 0;
	for (fi = lastPtUsed; fi < nbPt; fi++)
	  {
	    if (pts[fi].firstA >= 0 && swdData[pts[fi].firstA].misc == 0)
	      break;
	  }
	lastPtUsed = fi + 1;
	if (fi < nbPt)
	  {
	    int bestB = pts[fi].firstA;
	    if (bestB >= 0)
	      {
		startBord = bestB;
		if (fi == 0)
		  {
		    outsideW = 0;
		  }
		else
		  {
		    if (brutal)
		      {
			outsideW = Winding (pts[fi].x);
		      }
		    else
		      {
			outsideW = Winding (fi);
		      }
		  }
    if ( pts[fi].dI+pts[fi].dO == 1 ) {
      if ( fi == aretes[startBord].en ) {
        if ( eData[startBord].weight == 0 ) {
          // on se contente d'inverser
          Inverse(startBord);
        } else {
          // on passe le askForWinding (sinon ca va rester startBord)
          pData[aretes[startBord].st].askForWindingB=pData[aretes[startBord].en].askForWindingB;
        }
      }
    }
		if (aretes[startBord].en == fi)
		  outsideW += eData[startBord].weight;
	      }
	  }
      }
      if (startBord >= 0)
	{
	  // parcours en profondeur pour mettre les leF et riF a leurs valeurs
	  swdData[startBord].misc = (void *) 1;
	  swdData[startBord].leW = outsideW;
	  swdData[startBord].riW = outsideW - eData[startBord].weight;
//                                              printf("part de %d\n",startBord);
	  int curBord = startBord;
	  bool curDir = true;
	  swdData[curBord].precParc = -1;
	  swdData[curBord].suivParc = -1;
	  do
	    {
	      int cPt;
	      if (curDir)
		cPt = aretes[curBord].en;
	      else
		cPt = aretes[curBord].st;
	      int nb = curBord;
//                                                              printf("de curBord= %d avec leF= %d et riF= %d  -> ",curBord,swdData[curBord].leW,swdData[curBord].riW);
	      do
		{
		  int nnb = -1;
		  if (aretes[nb].en == cPt)
		    {
		      outsideW = swdData[nb].riW;
		      nnb = CyclePrevAt (cPt, nb);
		    }
		  else
		    {
		      outsideW = swdData[nb].leW;
		      nnb = CyclePrevAt (cPt, nb);
		    }
		  if (nnb == nb)
		    {
		      // cul-de-sac
		      nb = -1;
		      break;
		    }
		  nb = nnb;
		}
	      while (nb >= 0 && nb != curBord && swdData[nb].misc != 0);
	      if (nb < 0 || nb == curBord)
		{
		  // retour en arriere
		  int oPt;
		  if (curDir)
		    oPt = aretes[curBord].st;
		  else
		    oPt = aretes[curBord].en;
		  curBord = swdData[curBord].precParc;
//                                                                              printf("retour vers %d\n",curBord);
		  if (curBord < 0)
		    break;
		  if (oPt == aretes[curBord].en)
		    curDir = true;
		  else
		    curDir = false;
		}
	      else
		{
		  swdData[nb].misc = (void *) 1;
		  swdData[nb].ind = searchInd++;
		  if (cPt == aretes[nb].st)
		    {
		      swdData[nb].riW = outsideW;
		      swdData[nb].leW = outsideW + eData[nb].weight;
		    }
		  else
		    {
		      swdData[nb].leW = outsideW;
		      swdData[nb].riW = outsideW - eData[nb].weight;
		    }
		  swdData[nb].precParc = curBord;
		  swdData[curBord].suivParc = nb;
		  curBord = nb;
		  //                                                                      printf("suite %d\n",curBord);
		  if (cPt == aretes[nb].en)
		    curDir = false;
		  else
		    curDir = true;
		}
	    }
	  while (1 /*swdData[curBord].precParc >= 0 */ );
	  // fin du cas non-oriente
	}
    }
  while (lastPtUsed < nbPt);
//      fflush(stdout);
}

bool
Shape::TesteIntersection (Shape * ils, Shape * irs, int ilb, int irb,
                          NR::Point &atx, double &atL, double &atR,
			  bool onlyDiff)
{
  int lSt = ils->aretes[ilb].st, lEn = ils->aretes[ilb].en;
  int rSt = irs->aretes[irb].st, rEn = irs->aretes[irb].en;
  if (lSt == rSt || lSt == rEn)
    {
      return false;
    }
  if (lEn == rSt || lEn == rEn)
    {
      return false;
    }

  NR::Point ldir, rdir;
  ldir = ils->eData[ilb].rdx;
  rdir = irs->eData[irb].rdx;

  double il = ils->pData[lSt].rx[0], it = ils->pData[lSt].rx[1], ir =
    ils->pData[lEn].rx[0], ib = ils->pData[lEn].rx[1];
  if (il > ir)
    {
      double swf = il;
      il = ir;
      ir = swf;
    }
  if (it > ib)
    {
      double swf = it;
      it = ib;
      ib = swf;
    }
  double jl = irs->pData[rSt].rx[0], jt = irs->pData[rSt].rx[1], jr =
    irs->pData[rEn].rx[0], jb = irs->pData[rEn].rx[1];
  if (jl > jr)
    {
      double swf = jl;
      jl = jr;
      jr = swf;
    }
  if (jt > jb)
    {
      double swf = jt;
      jt = jb;
      jb = swf;
    }

  if (il > jr || it > jb || ir < jl || ib < jt)
    return false;

  // pre-test
  {
    NR::Point sDiff, eDiff;
    double slDot, elDot;
    double srDot, erDot;
    sDiff = ils->pData[lSt].rx - irs->pData[rSt].rx;
    eDiff = ils->pData[lEn].rx - irs->pData[rSt].rx;
    srDot = cross (sDiff,rdir );
    erDot = cross (eDiff,rdir );
    if ((srDot >= 0 && erDot >= 0) || (srDot <= 0 && erDot <= 0))
      return false;

    sDiff = irs->pData[rSt].rx - ils->pData[lSt].rx;
    eDiff = irs->pData[rEn].rx - ils->pData[lSt].rx;
    slDot = cross (sDiff,ldir );
    elDot = cross (eDiff,ldir);
    if ((slDot >= 0 && elDot >= 0) || (slDot <= 0 && elDot <= 0))
      return false;

    double slb = slDot - elDot, srb = srDot - erDot;
    if (slb < 0)
      slb = -slb;
    if (srb < 0)
      srb = -srb;
    if (slb > srb)
      {
	atx =
	  (slDot * irs->pData[rEn].rx - elDot * irs->pData[rSt].rx) / (slDot -
								       elDot);
      }
    else
      {
	atx =
	  (srDot * ils->pData[lEn].rx - erDot * ils->pData[lSt].rx) / (srDot -
								       erDot);
      }
    atL = srDot / (srDot - erDot);
    atR = slDot / (slDot - elDot);
    return true;
  }

  // a mettre en double precision pour des resultats exacts
  NR::Point usvs;
  usvs = irs->pData[rSt].rx - ils->pData[lSt].rx;

  // pas sur de l'ordre des coefs de m
  NR::Matrix m(ldir[0], ldir[1],
	       rdir[0], rdir[1],
	       0, 0);
  double det = m.det();

  double tdet = det * ils->eData[ilb].isqlength * irs->eData[irb].isqlength;

  if (tdet > -0.0001 && tdet < 0.0001)
    {				// ces couillons de vecteurs sont colineaires
      NR::Point sDiff, eDiff;
      double sDot, eDot;
      sDiff = ils->pData[lSt].rx - irs->pData[rSt].rx;
      eDiff = ils->pData[lEn].rx - irs->pData[rSt].rx;
      sDot = cross (sDiff,rdir );
      eDot = cross (eDiff,rdir);

      atx =
	(sDot * irs->pData[lEn].rx - eDot * irs->pData[lSt].rx) / (sDot -
								   eDot);
      atL = sDot / (sDot - eDot);

      sDiff = irs->pData[rSt].rx - ils->pData[lSt].rx;
       eDiff = irs->pData[rEn].rx - ils->pData[lSt].rx;
      sDot = cross (sDiff,ldir );
      eDot = cross (eDiff,ldir );

      atR = sDot / (sDot - eDot);

      return true;
    }

  // plus de colinearite ni d'extremites en commun
  m[1] = -m[1];
  m[2] = -m[2];
  {
    double swap = m[0];
    m[0] = m[3];
    m[3] = swap;
  }

  atL = (m[0]* usvs[0] + m[1] * usvs[1]) / det;
  atR = -(m[2] * usvs[0] + m[3] * usvs[1]) / det;
  atx = ils->pData[lSt].rx + atL * ldir;


  return true;
}

bool
Shape::TesteAdjacency (Shape * a, int no, const NR::Point atx, int nPt,
		       bool push)
{
  if (nPt == a->swsData[no].stPt || nPt == a->swsData[no].enPt)
    return false;

  NR::Point adir, diff, ast, aen, diff1, diff2, diff3, diff4;

  ast = a->pData[a->aretes[no].st].rx;
  aen = a->pData[a->aretes[no].en].rx;

  adir = a->eData[no].rdx;

  double sle = a->eData[no].length;
  double ile = a->eData[no].ilength;

  diff = atx - ast;
 
  double e = IHalfRound ((cross (diff,adir)) * a->eData[no].isqlength);
  if (-3 < e && e < 3)
    {
      double rad = HalfRound (0.505);
      diff1[0] = diff[0] - rad;
      diff1[1] = diff[1] - rad;
      diff2[0] = diff[0] + rad;
      diff2[1] = diff[1] - rad;
      diff3[0] = diff[0] + rad;
      diff3[1] = diff[1] + rad;
      diff4[0] = diff[0] - rad;
      diff4[1] = diff[1] + rad;
      double di1, di2;
      bool adjacent = false;
      di1 = cross (diff1,adir);
      di2 = cross (diff3,adir);
      if ((di1 < 0 && di2 > 0) || (di1 > 0 && di2 < 0))
	{
	  adjacent = true;
	}
      else
	{
	  di1 = cross ( diff2,adir);
	  di2 = cross (diff4,adir);
	  if ((di1 < 0 && di2 > 0) || (di1 > 0 && di2 < 0))
	    {
	      adjacent = true;
	    }
	}
      if (adjacent)
	{
	  double t = dot (diff, adir);
	  if (t > 0 && t < sle)
	    {
	      if (push)
		{
		  t *= ile;
		  PushIncidence (a, no, nPt, t);
		}
	      return true;
	    }
	}
    }
  return false;
}

void
Shape::CheckAdjacencies (int lastPointNo, int lastChgtPt, Shape * shapeHead,
			 int edgeHead)
{
  for (int cCh = 0; cCh < nbChgt; cCh++)
    {
      int chLeN = chgts[cCh].ptNo;
      int chRiN = chgts[cCh].ptNo;
      if (chgts[cCh].src)
	{
	  Shape *lS = chgts[cCh].src;
	  int lB = chgts[cCh].bord;
	  int lftN = lS->swsData[lB].leftRnd;
	  int rgtN = lS->swsData[lB].rightRnd;
	  if (lftN < chLeN)
	    chLeN = lftN;
	  if (rgtN > chRiN)
	    chRiN = rgtN;
//                      for (int n=lftN;n<=rgtN;n++) CreateIncidence(lS,lB,n);
	  for (int n = lftN - 1; n >= lastChgtPt; n--)
	    {
	      if (TesteAdjacency (lS, lB, pts[n].x, n, false) ==
		  false)
		break;
	      lS->swsData[lB].leftRnd = n;
	    }
	  for (int n = rgtN + 1; n < lastPointNo; n++)
	    {
	      if (TesteAdjacency (lS, lB, pts[n].x, n, false) ==
		  false)
		break;
	      lS->swsData[lB].rightRnd = n;
	    }
	}
      if (chgts[cCh].osrc)
	{
	  Shape *rS = chgts[cCh].osrc;
	  int rB = chgts[cCh].obord;
	  int lftN = rS->swsData[rB].leftRnd;
	  int rgtN = rS->swsData[rB].rightRnd;
	  if (lftN < chLeN)
	    chLeN = lftN;
	  if (rgtN > chRiN)
	    chRiN = rgtN;
//                      for (int n=lftN;n<=rgtN;n++) CreateIncidence(rS,rB,n);
	  for (int n = lftN - 1; n >= lastChgtPt; n--)
	    {
	      if (TesteAdjacency (rS, rB, pts[n].x, n, false) ==
		  false)
		break;
	      rS->swsData[rB].leftRnd = n;
	    }
	  for (int n = rgtN + 1; n < lastPointNo; n++)
	    {
	      if (TesteAdjacency (rS, rB, pts[n].x, n, false) ==
		  false)
		break;
	      rS->swsData[rB].rightRnd = n;
	    }
	}
      if (chgts[cCh].lSrc)
	{
	  if (chgts[cCh].lSrc->swsData[chgts[cCh].lBrd].leftRnd < lastChgtPt)
	    {
	      Shape *nSrc = chgts[cCh].lSrc;
	      int nBrd = chgts[cCh].lBrd /*,nNo=chgts[cCh].ptNo */ ;
	      bool hit;

	      do
		{
		  hit = false;
		  for (int n = chRiN; n >= chLeN; n--)
		    {
		      if (TesteAdjacency
			  (nSrc, nBrd, pts[n].x, n, false))
			{
			  if (nSrc->swsData[nBrd].leftRnd < lastChgtPt)
			    {
			      nSrc->swsData[nBrd].leftRnd = n;
			      nSrc->swsData[nBrd].rightRnd = n;
			    }
			  else
			    {
			      if (n < nSrc->swsData[nBrd].leftRnd)
				nSrc->swsData[nBrd].leftRnd = n;
			      if (n > nSrc->swsData[nBrd].rightRnd)
				nSrc->swsData[nBrd].rightRnd = n;
			    }
			  hit = true;
			}
		    }
		  for (int n = chLeN - 1; n >= lastChgtPt; n--)
		    {
		      if (TesteAdjacency
			  (nSrc, nBrd, pts[n].x, n, false) == false)
			break;
		      if (nSrc->swsData[nBrd].leftRnd < lastChgtPt)
			{
			  nSrc->swsData[nBrd].leftRnd = n;
			  nSrc->swsData[nBrd].rightRnd = n;
			}
		      else
			{
			  if (n < nSrc->swsData[nBrd].leftRnd)
			    nSrc->swsData[nBrd].leftRnd = n;
			  if (n > nSrc->swsData[nBrd].rightRnd)
			    nSrc->swsData[nBrd].rightRnd = n;
			}
		      hit = true;
		    }
		  if (hit)
		    {
		      SweepTree *node =
			static_cast < SweepTree * >(nSrc->swsData[nBrd].misc);
		      if (node == NULL)
			break;
		      node = static_cast < SweepTree * >(node->leftElem);
		      if (node == NULL)
			break;
		      nSrc = node->src;
		      nBrd = node->bord;
		      if (nSrc->swsData[nBrd].leftRnd >= lastChgtPt)
			break;
		    }
		}
	      while (hit);

	    }
	}
      if (chgts[cCh].rSrc)
	{
	  if (chgts[cCh].rSrc->swsData[chgts[cCh].rBrd].leftRnd < lastChgtPt)
	    {
	      Shape *nSrc = chgts[cCh].rSrc;
	      int nBrd = chgts[cCh].rBrd /*,nNo=chgts[cCh].ptNo */ ;
	      bool hit;
	      do
		{
		  hit = false;
		  for (int n = chLeN; n <= chRiN; n++)
		    {
		      if (TesteAdjacency
			  (nSrc, nBrd, pts[n].x, n, false))
			{
			  if (nSrc->swsData[nBrd].leftRnd < lastChgtPt)
			    {
			      nSrc->swsData[nBrd].leftRnd = n;
			      nSrc->swsData[nBrd].rightRnd = n;
			    }
			  else
			    {
			      if (n < nSrc->swsData[nBrd].leftRnd)
				nSrc->swsData[nBrd].leftRnd = n;
			      if (n > nSrc->swsData[nBrd].rightRnd)
				nSrc->swsData[nBrd].rightRnd = n;
			    }
			  hit = true;
			}
		    }
		  for (int n = chRiN + 1; n < lastPointNo; n++)
		    {
		      if (TesteAdjacency
			  (nSrc, nBrd, pts[n].x, n, false) == false)
			break;
		      if (nSrc->swsData[nBrd].leftRnd < lastChgtPt)
			{
			  nSrc->swsData[nBrd].leftRnd = n;
			  nSrc->swsData[nBrd].rightRnd = n;
			}
		      else
			{
			  if (n < nSrc->swsData[nBrd].leftRnd)
			    nSrc->swsData[nBrd].leftRnd = n;
			  if (n > nSrc->swsData[nBrd].rightRnd)
			    nSrc->swsData[nBrd].rightRnd = n;
			}
		      hit = true;
		    }
		  if (hit)
		    {
		      SweepTree *node =
			static_cast < SweepTree * >(nSrc->swsData[nBrd].misc);
		      if (node == NULL)
			break;
		      node = static_cast < SweepTree * >(node->rightElem);
		      if (node == NULL)
			break;
		      nSrc = node->src;
		      nBrd = node->bord;
		      if (nSrc->swsData[nBrd].leftRnd >= lastChgtPt)
			break;
		    }
		}
	      while (hit);
	    }
	}
    }
}

void
Shape::AddChgt (int lastPointNo, int lastChgtPt, Shape * &shapeHead,
		int &edgeHead, int type, Shape * lS, int lB, Shape * rS,
		int rB)
{
  if (nbChgt >= maxChgt)
    {
      maxChgt = 2 * nbChgt + 1;
      chgts = (sTreeChange *) realloc (chgts, maxChgt * sizeof (sTreeChange));
    }
  int nCh = nbChgt++;
  chgts[nCh].ptNo = lastPointNo;
  chgts[nCh].type = type;
  chgts[nCh].src = lS;
  chgts[nCh].bord = lB;
  chgts[nCh].osrc = rS;
  chgts[nCh].obord = rB;
  if (lS)
    {
      SweepTree *lE = static_cast < SweepTree * >(lS->swsData[lB].misc);
      if (lE && lE->leftElem)
	{
	  SweepTree *llE = static_cast < SweepTree * >(lE->leftElem);
	  chgts[nCh].lSrc = llE->src;
	  chgts[nCh].lBrd = llE->bord;
	}
      else
	{
	  chgts[nCh].lSrc = NULL;
	  chgts[nCh].lBrd = -1;
	}

      if (lS->swsData[lB].leftRnd < lastChgtPt)
	{
	  lS->swsData[lB].leftRnd = lastPointNo;
	  lS->swsData[lB].nextSh = shapeHead;
	  lS->swsData[lB].nextBo = edgeHead;
	  edgeHead = lB;
	  shapeHead = lS;
	}
      else
	{
	  int old = lS->swsData[lB].leftRnd;
	  if (pts[old].x[0] > pts[lastPointNo].x[0])
	    lS->swsData[lB].leftRnd = lastPointNo;
	}
      if (lS->swsData[lB].rightRnd < lastChgtPt)
	{
	  lS->swsData[lB].rightRnd = lastPointNo;
	}
      else
	{
	  int old = lS->swsData[lB].rightRnd;
	  if (pts[old].x[0] < pts[lastPointNo].x[0])
	    lS->swsData[lB].rightRnd = lastPointNo;
	}
    }

  if (rS)
    {
      SweepTree *rE = static_cast < SweepTree * >(rS->swsData[rB].misc);
      if (rE->rightElem)
	{
	  SweepTree *rrE = static_cast < SweepTree * >(rE->rightElem);
	  chgts[nCh].rSrc = rrE->src;
	  chgts[nCh].rBrd = rrE->bord;
	}
      else
	{
	  chgts[nCh].rSrc = NULL;
	  chgts[nCh].rBrd = -1;
	}

      if (rS->swsData[rB].leftRnd < lastChgtPt)
	{
	  rS->swsData[rB].leftRnd = lastPointNo;
	  rS->swsData[rB].nextSh = shapeHead;
	  rS->swsData[rB].nextBo = edgeHead;
	  edgeHead = rB;
	  shapeHead = rS;
	}
      else
	{
	  int old = rS->swsData[rB].leftRnd;
	  if (pts[old].x[0] > pts[lastPointNo].x[0])
	    rS->swsData[rB].leftRnd = lastPointNo;
	}
      if (rS->swsData[rB].rightRnd < lastChgtPt)
	{
	  rS->swsData[rB].rightRnd = lastPointNo;
	}
      else
	{
	  int old = rS->swsData[rB].rightRnd;
	  if (pts[old].x[0] < pts[lastPointNo].x[0])
	    rS->swsData[rB].rightRnd = lastPointNo;
	}
    }
  else
    {
      SweepTree *lE = static_cast < SweepTree * >(lS->swsData[lB].misc);
      if (lE && lE->rightElem)
	{
	  SweepTree *rlE = static_cast < SweepTree * >(lE->rightElem);
	  chgts[nCh].rSrc = rlE->src;
	  chgts[nCh].rBrd = rlE->bord;
	}
      else
	{
	  chgts[nCh].rSrc = NULL;
	  chgts[nCh].rBrd = -1;
	}
    }
}
void
Shape::Validate (void)
{
  for (int i = 0; i < nbPt; i++)
    {
      pData[i].rx = pts[i].x;
    }
  for (int i = 0; i < nbAr; i++)
    {
      eData[i].rdx = aretes[i].dx;
    }
  for (int i = 0; i < nbAr; i++)
    {
      for (int j = i + 1; j < nbAr; j++)
	{
        NR::Point atx;
        double   atL, atR;
	  if (TesteIntersection (this, this, i, j, atx, atL, atR, false))
	    {
	      printf ("%i %i  %f %f \n", i, j, atx[0],atx[1]);
	    }
	}
    }
  fflush (stdout);
}

void
Shape::CheckEdges (int lastPointNo, int lastChgtPt, Shape * a, Shape * b,
		   BooleanOp mod)
{

  for (int cCh = 0; cCh < nbChgt; cCh++)
    {
      if (chgts[cCh].type == 0)
	{
	  Shape *lS = chgts[cCh].src;
	  int lB = chgts[cCh].bord;
	  lS->swsData[lB].curPoint = chgts[cCh].ptNo;
	}
    }
  for (int cCh = 0; cCh < nbChgt; cCh++)
    {
//              int   chLeN=chgts[cCh].ptNo;
//              int   chRiN=chgts[cCh].ptNo;
      if (chgts[cCh].src)
	{
	  Shape *lS = chgts[cCh].src;
	  int lB = chgts[cCh].bord;
	  Avance (lastPointNo, lastChgtPt, lS, lB, a, b, mod);
	}
      if (chgts[cCh].osrc)
	{
	  Shape *rS = chgts[cCh].osrc;
	  int rB = chgts[cCh].obord;
	  Avance (lastPointNo, lastChgtPt, rS, rB, a, b, mod);
	}
      if (chgts[cCh].lSrc)
	{
	  Shape *nSrc = chgts[cCh].lSrc;
	  int nBrd = chgts[cCh].lBrd;
	  while (nSrc->swsData[nBrd].leftRnd >=
		 lastChgtPt /*&& nSrc->swsData[nBrd].doneTo < lastChgtPt */ )
	    {
	      Avance (lastPointNo, lastChgtPt, nSrc, nBrd, a, b, mod);

	      SweepTree *node =
		static_cast < SweepTree * >(nSrc->swsData[nBrd].misc);
	      if (node == NULL)
		break;
	      node = static_cast < SweepTree * >(node->leftElem);
	      if (node == NULL)
		break;
	      nSrc = node->src;
	      nBrd = node->bord;
	    }
	}
      if (chgts[cCh].rSrc)
	{
	  Shape *nSrc = chgts[cCh].rSrc;
	  int nBrd = chgts[cCh].rBrd;
	  while (nSrc->swsData[nBrd].rightRnd >=
		 lastChgtPt /*&& nSrc->swsData[nBrd].doneTo < lastChgtPt */ )
	    {
	      Avance (lastPointNo, lastChgtPt, nSrc, nBrd, a, b, mod);

	      SweepTree *node =
		static_cast < SweepTree * >(nSrc->swsData[nBrd].misc);
	      if (node == NULL)
		break;
	      node = static_cast < SweepTree * >(node->rightElem);
	      if (node == NULL)
		break;
	      nSrc = node->src;
	      nBrd = node->bord;
	    }
	}
    }
}

void
Shape::Avance (int lastPointNo, int lastChgtPt, Shape * lS, int lB, Shape * a,
	       Shape * b, BooleanOp mod)
{
  double dd = HalfRound (1);
  bool avoidDiag = false;
//      if ( lastChgtPt > 0 && pts[lastChgtPt-1].y+dd == pts[lastChgtPt].y ) avoidDiag=true;

  bool direct = true;
  if (lS == b && (mod == bool_op_diff || mod == bool_op_symdiff))
    direct = false;
  int lftN = lS->swsData[lB].leftRnd;
  int rgtN = lS->swsData[lB].rightRnd;
  if (lS->swsData[lB].doneTo < lastChgtPt)
    {
      int lp = lS->swsData[lB].curPoint;
      if (lp >= 0 && pts[lp].x[1] + dd == pts[lastChgtPt].x[1])
	avoidDiag = true;
      if (lS->eData[lB].rdx[1] == 0)
	{
	  // tjs de gauche a droite et pas de diagonale
	  if (lS->eData[lB].rdx[0] >= 0)
	    {
	      for (int p = lftN; p <= rgtN; p++)
		{
		  DoEdgeTo (lS, lB, p, direct, true);
		  lp = p;
		}
	    }
	  else
	    {
	      for (int p = lftN; p <= rgtN; p++)
		{
		  DoEdgeTo (lS, lB, p, direct, false);
		  lp = p;
		}
	    }
	}
      else if (lS->eData[lB].rdx[1] > 0)
	{
	  if (lS->eData[lB].rdx[0] >= 0)
	    {

	      for (int p = lftN; p <= rgtN; p++)
		{
		  if (avoidDiag && p == lftN && pts[lftN].x[0] == pts[lp].x[0] + dd)
		    {
		      if (lftN > 0 && lftN - 1 >= lastChgtPt
			  && pts[lftN - 1].x[0] == pts[lp].x[0])
			{
			  DoEdgeTo (lS, lB, lftN - 1, direct, true);
			  DoEdgeTo (lS, lB, lftN, direct, true);
			}
		      else
			{
			  DoEdgeTo (lS, lB, lftN, direct, true);
			}
		    }
		  else
		    {
		      DoEdgeTo (lS, lB, p, direct, true);
		    }
		  lp = p;
		}
	    }
	  else
	    {

	      for (int p = rgtN; p >= lftN; p--)
		{
		  if (avoidDiag && p == rgtN && pts[rgtN].x[0] == pts[lp].x[0] - dd)
		    {
		      if (rgtN < nbPt && rgtN + 1 < lastPointNo
			  && pts[rgtN + 1].x[0] == pts[lp].x[0])
			{
			  DoEdgeTo (lS, lB, rgtN + 1, direct, true);
			  DoEdgeTo (lS, lB, rgtN, direct, true);
			}
		      else
			{
			  DoEdgeTo (lS, lB, rgtN, direct, true);
			}
		    }
		  else
		    {
		      DoEdgeTo (lS, lB, p, direct, true);
		    }
		  lp = p;
		}
	    }
	}
      else
	{
	  if (lS->eData[lB].rdx[0] >= 0)
	    {

	      for (int p = rgtN; p >= lftN; p--)
		{
		  if (avoidDiag && p == rgtN && pts[rgtN].x[0] == pts[lp].x[0] - dd)
		    {
		      if (rgtN < nbPt && rgtN + 1 < lastPointNo
			  && pts[rgtN + 1].x[0] == pts[lp].x[0])
			{
			  DoEdgeTo (lS, lB, rgtN + 1, direct, false);
			  DoEdgeTo (lS, lB, rgtN, direct, false);
			}
		      else
			{
			  DoEdgeTo (lS, lB, rgtN, direct, false);
			}
		    }
		  else
		    {
		      DoEdgeTo (lS, lB, p, direct, false);
		    }
		  lp = p;
		}
	    }
	  else
	    {

	      for (int p = lftN; p <= rgtN; p++)
		{
		  if (avoidDiag && p == lftN && pts[lftN].x[0] == pts[lp].x[0] + dd)
		    {
		      if (lftN > 0 && lftN - 1 >= lastChgtPt
			  && pts[lftN - 1].x[0] == pts[lp].x[0])
			{
			  DoEdgeTo (lS, lB, lftN - 1, direct, false);
			  DoEdgeTo (lS, lB, lftN, direct, false);
			}
		      else
			{
			  DoEdgeTo (lS, lB, lftN, direct, false);
			}
		    }
		  else
		    {
		      DoEdgeTo (lS, lB, p, direct, false);
		    }
		  lp = p;
		}
	    }
	}
      lS->swsData[lB].curPoint = lp;
    }
  lS->swsData[lB].doneTo = lastPointNo - 1;
}

void
Shape::DoEdgeTo (Shape * iS, int iB, int iTo, bool direct, bool sens)
{
  int lp = iS->swsData[iB].curPoint;
  int ne = -1;
  if (sens)
    {
      if (direct)
	ne = AddEdge (lp, iTo);
      else
	ne = AddEdge (iTo, lp);
    }
  else
    {
      if (direct)
	ne = AddEdge (iTo, lp);
      else
	ne = AddEdge (lp, iTo);
    }
  if (ne >= 0 && HasBackData ())
    {
      ebData[ne].pathID = iS->ebData[iB].pathID;
      ebData[ne].pieceID = iS->ebData[iB].pieceID;
      if (iS->eData[iB].length < 0.00001)
	{
	  ebData[ne].tSt = ebData[ne].tEn = iS->ebData[iB].tSt;
	}
      else
	{
	  double bdl = iS->eData[iB].ilength;
    NR::Point bpx = iS->pData[iS->aretes[iB].st].rx;
	  NR::Point bdx = iS->eData[iB].rdx;
	  NR::Point psx = pts[aretes[ne].st].x;
	  NR::Point pex = pts[aretes[ne].en].x;
        NR::Point psbx=psx-bpx;
        NR::Point pebx=pex-bpx;
	  double pst = dot(psbx,bdx) * bdl;
	  double pet = dot(pebx,bdx) * bdl;
	  pst = iS->ebData[iB].tSt * (1 - pst) + iS->ebData[iB].tEn * pst;
	  pet = iS->ebData[iB].tSt * (1 - pet) + iS->ebData[iB].tEn * pet;
	  ebData[ne].tEn = pet;
	  ebData[ne].tSt = pst;
	}
    }
  iS->swsData[iB].curPoint = iTo;
  if (ne >= 0)
    {
      int cp = iS->swsData[iB].firstLinkedPoint;
      swsData[ne].firstLinkedPoint = iS->swsData[iB].firstLinkedPoint;
      while (cp >= 0)
	{
	  pData[cp].askForWindingB = ne;
	  cp = pData[cp].nextLinkedPoint;
	}
      iS->swsData[iB].firstLinkedPoint = -1;
    }
}
